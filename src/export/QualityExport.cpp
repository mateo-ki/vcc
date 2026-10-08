#include "export/QualityExport.h"

#include <QByteArray>
#include <QHostAddress>
#include <QString>
#include <QTcpSocket>
#include <QVector>
#include <QtGlobal>

#include <QElapsedTimer>
#include <QSslSocket>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace ExportQuality {
namespace {

enum class SecurityKind { Reality, Tls, None, Unknown };

constexpr qsizetype kProbeConcurrency = 64;

QString toAsciiLower(const QString &text)
{
    QString out = text;
    for (QChar &ch : out) {
        if (ch.unicode() >= u'A' && ch.unicode() <= u'Z') {
            ch = QChar(static_cast<ushort>(ch.unicode() + 32));
        }
    }
    return out;
}

QString trimRs(const QString &text)
{
    return text.trimmed();
}

const Json *member(const Json &value, const char *key)
{
    const auto *obj = value.get_ptr<const Json::object_t *>();
    if (obj == nullptr) {
        return nullptr;
    }
    const auto found = obj->find(std::string(key));
    return found == obj->end() ? nullptr : &found->second;
}

/// export_quality.rs 的 first_str：空串不算命中（与 export.rs 的 first_str 不同），
/// 布尔/数字转文本。null QString = None。
QString firstStr(const Json &cfg, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const Json *value = member(cfg, key);
        if (value == nullptr) {
            continue;
        }
        if (value->is_string()) {
            const QString text = QString::fromStdString(value->get<std::string>());
            if (!text.isEmpty()) {
                return text;
            }
        } else if (value->is_boolean()) {
            return value->get<bool>() ? QStringLiteral("true") : QStringLiteral("false");
        } else if (value->is_number()) {
            return QString::fromStdString(value->dump());
        }
    }
    return QString();
}

bool isTrueFlag(const Json &cfg, const char *key)
{
    const Json *value = member(cfg, key);
    return value != nullptr && value->is_boolean() && value->get<bool>();
}

SecurityKind securityKind(const Json &cfg)
{
    if (member(cfg, "reality-opts") != nullptr) {
        return SecurityKind::Reality;
    }
    const QString s = toAsciiLower(firstStr(cfg, {"security", "tls"}));
    if (s == QLatin1String("reality")) {
        return SecurityKind::Reality;
    }
    if (s == QLatin1String("tls") || s == QLatin1String("true") || s == QLatin1String("1")) {
        return SecurityKind::Tls;
    }
    // 布尔 tls 要在"空串"判定之前，这样 {"tls": true} 也能算 TLS。
    if (isTrueFlag(cfg, "tls")) {
        return SecurityKind::Tls;
    }
    if (s.isEmpty()) {
        // trojan 隐含就是 TLS，但这里只能给 Unknown，由调用方结合协议判断。
        return SecurityKind::Unknown;
    }
    if (s == QLatin1String("none") || s == QLatin1String("false") || s == QLatin1String("0")) {
        return SecurityKind::None;
    }
    if (s == QLatin1String("auto")) {
        // vmess 的 scy=auto 是加密算法不是安全类型。
        return SecurityKind::Unknown;
    }
    return SecurityKind::Unknown;
}

bool isRealityFamily(SecurityKind kind)
{
    return kind == SecurityKind::Tls || kind == SecurityKind::Reality;
}

bool hasCredentials(const Models::Node &node)
{
    const Json &cfg = node.config;
    const QString proto = toAsciiLower(node.protocol);
    if (proto == QLatin1String("vless") || proto == QLatin1String("vmess")) {
        return !firstStr(cfg, {"uuid", "id"}).isEmpty();
    }
    if (proto == QLatin1String("trojan") || proto == QLatin1String("hysteria2")
        || proto == QLatin1String("hy2") || proto == QLatin1String("hysteria")) {
        return !firstStr(cfg, {"password", "auth", "auth-str"}).isEmpty();
    }
    if (proto == QLatin1String("shadowsocks") || proto == QLatin1String("ss")
        || proto == QLatin1String("shadowsocksr") || proto == QLatin1String("ssr")) {
        return !firstStr(cfg, {"password"}).isEmpty()
               && !firstStr(cfg, {"cipher", "method"}).isEmpty();
    }
    return true;
}

bool needsTls(const Models::Node &node)
{
    const QString proto = toAsciiLower(node.protocol);
    if (proto == QLatin1String("trojan")) {
        return true;
    }
    return isRealityFamily(securityKind(node.config));
}

QString sniFor(const Models::Node &node)
{
    const QString sni = firstStr(node.config, {"sni", "servername", "host"});
    if (!sni.isEmpty()) {
        return sni;
    }
    return node.server;
}

/// rustls 的 DnsName 校验（RFC1035，但允许下划线）：总长 1..=253、标签 1..=63、
/// 标签不能以 - 开头或结尾、最后一个标签不能全数字（免得和 IPv4 混淆）。
bool dnsNameIsValid(const QByteArray &input)
{
    enum class LabelState { Start, HasLetter, AllNumeric, EndsInHyphen };
    const int len = static_cast<int>(input.size());
    if (len == 0 || len > 253) {
        return false;
    }
    int start = 0;
    int idx = 0;
    LabelState state = LabelState::Start;
    while (true) {
        if (idx >= len) {
            const int labelLength = idx - start;
            if (labelLength == 0 || labelLength > 63 || state == LabelState::EndsInHyphen) {
                return false;
            }
            break;
        }
        const auto ch = static_cast<unsigned char>(input.at(idx));
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_') {
            state = LabelState::HasLetter;
        } else if (ch >= '0' && ch <= '9') {
            state = (state == LabelState::Start || state == LabelState::AllNumeric)
                        ? LabelState::AllNumeric
                        : LabelState::HasLetter;
        } else if (ch == '.') {
            const int labelLength = idx - start;
            if (labelLength == 0 || labelLength > 63 || state == LabelState::EndsInHyphen) {
                return false;
            }
            if (idx + 1 >= len) {
                // 结尾点号：保留 state，交给下面的全数字判定。
                break;
            }
            idx += 1;
            start = idx;
            state = LabelState::Start;
            continue;
        } else if (ch == '-') {
            if (idx == start) {
                return false;
            }
            state = LabelState::EndsInHyphen;
        } else {
            return false;
        }
        idx += 1;
    }
    return state != LabelState::AllNumeric;
}

/// ServerName::try_from：先当 DNS 名，再当 IP，都不是就判定握手失败（Rust 直接返回 false）。
bool serverNameIsValid(const QString &sni)
{
    if (sni.isEmpty()) {
        return false;
    }
    if (dnsNameIsValid(sni.toUtf8())) {
        return true;
    }
    QHostAddress address;
    return address.setAddress(sni);
}

/// 明文节点的 HTTP 诱饵判定：不发请求对方就回 HTTP 头的，多半是开放端口/网站。
bool looksLikeImmediateHttp(QTcpSocket &socket, qint64 budgetMs)
{
    if (budgetMs <= 0) {
        return false;
    }
    static const char kProbe[] = "GET / HTTP/1.0\r\nHost: x\r\n\r\n";
    socket.write(kProbe, static_cast<qint64>(sizeof(kProbe) - 1));
    socket.waitForBytesWritten(static_cast<int>(std::min<qint64>(budgetMs, 250)));

    QByteArray buffer(16, '\0');
    if (!socket.waitForReadyRead(static_cast<int>(std::min<qint64>(budgetMs, 350)))) {
        return false;
    }
    const qint64 read = socket.read(buffer.data(), buffer.size());
    if (read <= 0) {
        return false;
    }
    // Rust 用 String::from_utf8_lossy：非法字节替换成 U+FFFD，Qt 的 fromUtf8 同义。
    const QString decoded = QString::fromUtf8(buffer.left(static_cast<qsizetype>(read)));
    QByteArray lowered = decoded.toUtf8();
    for (char &ch : lowered) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch + 32);
        }
    }
    const QString text = QString::fromLatin1(lowered);
    return text.startsWith(QLatin1String("http/")) || text.startsWith(QLatin1String("<!do"))
           || text.startsWith(QLatin1String("<html"));
}

/// TLS 握手（证书一律不校验，等价 rustls 的 NoVerify 验证器）。
bool tlsHandshake(const QString &host, quint16 port, const QString &sni, qint64 budgetMs)
{
    if (budgetMs <= 0) {
        return false;
    }
    if (!serverNameIsValid(sni)) {
        return false;
    }
    // Qt 的 QSslSocket 不能接管一个已连接的 QTcpSocket（Rust 是把 TcpStream 交给
    // tokio-rustls），所以这里让 QSslSocket 自己完成 TCP + 握手：仍然只建一条连接，
    // SNI 用 sni 而连接目标用 host；预算按"连接用掉多少，握手就用剩下的"切分，
    // 和 Rust 的 timeout.saturating_sub(start.elapsed()) 一致。
    QElapsedTimer clock;
    clock.start();
    QSslSocket socket;
    socket.setPeerVerifyMode(QSslSocket::VerifyNone);
    socket.ignoreSslErrors();
    socket.connectToHostEncrypted(host, port, sni);
    if (!socket.waitForConnected(static_cast<int>(budgetMs))) {
        return false;
    }
    const qint64 elapsed = clock.elapsed();
    const qint64 remaining = elapsed < budgetMs ? budgetMs - elapsed : 0;
    if (remaining <= 0) {
        return false;
    }
    return socket.waitForEncrypted(static_cast<int>(remaining));
}

} // namespace

qint64 exportScore(const Models::Node &node)
{
    qint64 score = 0;
    const QString proto = toAsciiLower(node.protocol);
    const Json &cfg = node.config;

    // 基线：TCP 延迟越低越好，但要压在一个固定量级上，不能唯延迟论。
    if (node.latencyMs.has_value()) {
        const qint64 clamped = std::clamp<qint64>(*node.latencyMs, 1, 19999);
        score += 20000 - clamped;
    }

    if (proto == QLatin1String("vless")) {
        score += 300;
    } else if (proto == QLatin1String("trojan")) {
        score += 280;
    } else if (proto == QLatin1String("shadowsocks") || proto == QLatin1String("ss")) {
        score += 250;
    } else if (proto == QLatin1String("vmess")) {
        score += 200;
    } else if (proto == QLatin1String("hysteria2") || proto == QLatin1String("hy2")) {
        score += 220;
    } else if (proto == QLatin1String("shadowsocksr") || proto == QLatin1String("ssr")) {
        score -= 200;
    } else if (proto == QLatin1String("http") || proto == QLatin1String("https")
               || proto == QLatin1String("socks5") || proto == QLatin1String("socks")) {
        score -= 500;
    }

    const SecurityKind security = securityKind(cfg);
    switch (security) {
    case SecurityKind::Reality:
        score += 2000;
        break;
    case SecurityKind::Tls:
        score += 1200;
        break;
    case SecurityKind::None:
        score -= 800;
        break;
    case SecurityKind::Unknown:
        score -= 200;
        break;
    }

    score += hasCredentials(node) ? 400 : -2000;

    const QString network = toAsciiLower(firstStr(cfg, {"network", "net"}));
    if (network == QLatin1String("ws") || network == QLatin1String("websocket")
        || network == QLatin1String("grpc") || network == QLatin1String("h2")
        || network == QLatin1String("httpupgrade") || network == QLatin1String("xhttp")) {
        score += 80;
    } else if (network == QLatin1String("tcp") || network == QLatin1String("raw")
               || network.isEmpty()) {
        score += 40;
    }

    // 端口启发式：裸 80 且没有 TLS 的基本是垃圾/开放 CDN。
    static const QVector<int> kTlsPorts{443, 8443, 2053, 2083, 2087, 2096};
    static const QVector<int> kPlainJunkPorts{80, 8080, 8880, 2052, 2082, 2086, 2095};
    static const QVector<int> kPlainTlsPorts{80, 8080, 8880};
    const bool tlsFamily = isRealityFamily(security);
    if (kTlsPorts.contains(node.port) && tlsFamily) {
        score += 120;
    } else if (kPlainJunkPorts.contains(node.port) && security == SecurityKind::None) {
        score -= 600;
    } else if (kPlainTlsPorts.contains(node.port) && tlsFamily) {
        score += 40;
    }

    // TLS 家族有没有 SNI。
    if (tlsFamily) {
        const QString sni = firstStr(cfg, {"sni", "servername", "host"});
        score += sni.isEmpty() ? -80 : 100;
    }

    // 营销名虽然弱，但仍是排序信号。
    const QString name = toAsciiLower(node.name);
    if (name.contains(QLatin1String("telegram")) || name.contains(QLatin1String("t.me"))
        || name.contains(QLatin1String("join")) || name.contains(QLatin1String("http"))) {
        score -= 150;
    }

    return score;
}

bool isStrictExportCandidate(const Models::Node &node)
{
    if (!hasCredentials(node)) {
        return false;
    }
    const QString proto = toAsciiLower(node.protocol);
    if (proto == QLatin1String("http") || proto == QLatin1String("https")
        || proto == QLatin1String("socks") || proto == QLatin1String("socks5")
        || proto == QLatin1String("mieru") || proto == QLatin1String("shadowsocksr")
        || proto == QLatin1String("ssr")) {
        return false;
    }

    // 现代免费订阅里这些协议本来就该有 TLS/REALITY。明文 none 的开放端口
    // 是 v2rayN 显示 -1 的主要来源。
    if (proto == QLatin1String("vless") || proto == QLatin1String("vmess")
        || proto == QLatin1String("trojan")) {
        return isRealityFamily(securityKind(node.config));
    }
    if (proto == QLatin1String("shadowsocks") || proto == QLatin1String("ss")) {
        const QString cipher = toAsciiLower(firstStr(node.config, {"cipher", "method"}));
        return !cipher.isEmpty() && cipher != QLatin1String("none");
    }
    return true;
}

bool liveProbeNode(const Models::Node &node, quint32 timeoutMs)
{
    const QString host = trimRs(node.server);
    if (host.isEmpty() || node.port == 0) {
        return false;
    }
    // Rust 的 tokio timeout(0) 是立刻失败；Qt 把 0 当"用默认 30s"，所以抬到 1ms。
    const int msecs = timeoutMs == 0 ? 1 : static_cast<int>(timeoutMs);

    QElapsedTimer clock;
    clock.start();

    const qint64 budget = msecs;
    if (!needsTls(node)) {
        QTcpSocket socket;
        socket.connectToHost(host, static_cast<quint16>(node.port));
        if (!socket.waitForConnected(static_cast<int>(budget))) {
            return false;
        }
        // 明文 SS / none 安全：TCP 连得上就是能便宜验证的全部了，
        // 只再排除一眼就是网站的。
        const qint64 elapsed = clock.elapsed();
        const qint64 remaining = elapsed < budget ? budget - elapsed : 0;
        return !looksLikeImmediateHttp(socket, remaining);
    }

    return tlsHandshake(host, static_cast<quint16>(node.port), sniFor(node), budget);
}

QVector<Models::Node> liveFilterForExport(QVector<Models::Node> nodes, quint32 want,
                                          quint32 timeoutMs)
{
    if (nodes.isEmpty() || want == 0) {
        return {};
    }
    const quint32 timeout = qMax<quint32>(timeoutMs, 300);
    const qsizetype total = nodes.size();

    struct Shared {
        std::mutex mutex;
        std::condition_variable changed;
        std::deque<qsizetype> survivors;
        std::atomic<qsizetype> claimed{0};
        qsizetype finished = 0;
        bool stop = false;
    } shared;

    auto probeNext = [&shared, &nodes, timeout]() {
        while (true) {
            {
                std::scoped_lock lock(shared.mutex);
                if (shared.stop) {
                    return;
                }
            }
            const qsizetype index = shared.claimed.fetch_add(1);
            if (index >= nodes.size()) {
                return;
            }
            const bool ok = liveProbeNode(nodes.at(index), timeout);
            std::scoped_lock lock(shared.mutex);
            if (ok) {
                shared.survivors.push_back(index);
            }
            shared.finished += 1;
            shared.changed.notify_all();
        }
    };

    const qsizetype workerCount = std::min<qsizetype>(kProbeConcurrency, total);
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(workerCount));
    for (qsizetype i = 0; i < workerCount; ++i) {
        workers.emplace_back(probeNext);
    }

    QVector<Models::Node> kept;
    kept.reserve(static_cast<qsizetype>(want));
    {
        std::unique_lock lock(shared.mutex);
        shared.changed.wait(lock, [&] {
            return shared.survivors.size() >= static_cast<qsizetype>(want)
                   || shared.finished == total;
        });
        while (!shared.survivors.empty() && kept.size() < static_cast<qsizetype>(want)) {
            kept.append(nodes.at(shared.survivors.front()));
            shared.survivors.pop_front();
        }
        shared.stop = true;
    }
    shared.changed.notify_all();
    // Rust 靠丢弃 JoinSet 直接 abort 剩余探测；这里必须 join，
    // 代价是最多多等一个 timeout 的在途探测。
    for (std::thread &worker : workers) {
        worker.join();
    }
    return kept;
}

} // namespace ExportQuality
