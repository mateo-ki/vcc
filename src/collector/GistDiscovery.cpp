#include "collector/GistDiscovery.h"

#include <QRegularExpression>
#include <QUrl>

#include "collector/Collector.h"

namespace Collector {

const QVector<QString> &gistDiscoveryQueries()
{
    // collector/mod.rs:66-74
    static const QVector<QString> queries = {
        QStringLiteral("vless//"),   QStringLiteral("vmess//"),  QStringLiteral("ss//"),
        QStringLiteral("trojan//"),  QStringLiteral("hysteria2//"),
        QStringLiteral("socks5//"),  QStringLiteral("clash/proxies"),
    };
    return queries;
}

Json GistDiscoverySummary::toJson() const
{
    // Rust derive(serde::Serialize) 按字段名原样输出 snake_case。
    Json value;
    value["pages_scanned"] = pagesScanned;
    value["registered"] = registered;
    value["nodes"] = nodes;
    value["skipped"] = skipped;
    value["rejected"] = rejected;
    value["pending"] = pending;
    value["stopped_reason"] = stoppedReason.toStdString();
    return value;
}

QString percentEncodeNonAlphanumeric(const QString &text)
{
    // percent_encoding::NON_ALPHANUMERIC：ASCII 字母数字以外全编码，十六进制大写。
    static const char *kHex = "0123456789ABCDEF";
    const QByteArray utf8 = text.toUtf8();
    QString out;
    out.reserve(utf8.size() * 2);
    for (const char raw : utf8) {
        const unsigned char ch = static_cast<unsigned char>(raw);
        const bool alnum = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')
            || (ch >= '0' && ch <= '9');
        if (alnum) {
            out += QChar(static_cast<char16_t>(ch));
        } else {
            out += QLatin1String("%");
            out += QChar(static_cast<char16_t>(kHex[(ch >> 4) & 0x0F]));
            out += QChar(static_cast<char16_t>(kHex[ch & 0x0F]));
        }
    }
    return out;
}

QString buildGistSearchUrl(const QString &query)
{
    // :529-532
    return QStringLiteral("https://%1/search?o=desc&q=%2&s=updated")
        .arg(QLatin1String(kGistSearchHost), percentEncodeNonAlphanumeric(query));
}

bool parseAbsoluteUrl(const QString &url, QString &hostOut)
{
    // Rust 用的是 `reqwest::Url::parse`：相对串直接报错。QUrl 默认宽容模式会把
    // "notaurl" 当成相对 URL 收下，所以这里必须用 StrictMode 并要求有 scheme。
    const QUrl parsed(url, QUrl::StrictMode);
    if (!parsed.isValid() || parsed.scheme().isEmpty()) {
        hostOut = QString();
        return false;
    }
    hostOut = parsed.host();
    return true;
}

QString appendQueryParam(const QString &url, const QString &key, const QString &value)
{
    QString host;
    if (!parseAbsoluteUrl(url, host)) {
        return QString();
    }
    // query_pairs_mut().append_pair()：加在 query 末尾（#fragment 之前），
    // 原来没有 query 就用 '?' 起一个。
    const qsizetype fragment = url.indexOf(QLatin1Char('#'));
    const qsizetype head = fragment < 0 ? url.size() : fragment;
    const QString separator = url.left(head).contains(QLatin1Char('?')) ? QLatin1String("&")
                                                                       : QLatin1String("?");
    return url.left(head) + separator + key + QLatin1Char('=') + value + url.mid(head);
}

namespace {

const QRegularExpression &snippetSplitRegex()
{
    static const QRegularExpression regex(
        QStringLiteral(R"(<div class="gist-snippet">)"));
    return regex;
}

const QRegularExpression &gistAnchorRegex()
{
    // 行尾的引号天然排除了 /stargazers 之类子路径：id 后面跟的不是引号就不匹配。
    static const QRegularExpression regex(
        QStringLiteral(R"RE(href="/([A-Za-z0-9_-]+)/([0-9a-f]{16,40})")RE"));
    return regex;
}

const QRegularExpression &fileNameRegex()
{
    static const QRegularExpression regex(QStringLiteral(
        R"(>([A-Za-z0-9._-]{4,80}\.(?:txt|yaml|yml|md|json|list|conf|csv))<)"));
    return regex;
}

/// `SNIPPET_SPLIT.split(html).skip(1)`：按分隔符切，丢掉第一段（第一个块之前的文本）。
QVector<QString> snippetBlocks(const QString &html)
{
    QVector<QString> blocks;
    QRegularExpressionMatchIterator iterator = snippetSplitRegex().globalMatch(html);
    qsizetype cursor = 0;
    bool first = true;
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        const qsizetype start = match.capturedStart();
        if (!first) {
            blocks.append(html.mid(cursor, start - cursor));
        }
        first = false;
        cursor = start + match.capturedLength();
    }
    if (!first) {
        blocks.append(html.mid(cursor));
    }
    return blocks;
}

} // namespace

QVector<DiscoveredGist> extractGists(const QString &searchHtml)
{
    QVector<DiscoveredGist> found;
    for (const QString &block : snippetBlocks(searchHtml)) {
        const QRegularExpressionMatch anchor = gistAnchorRegex().match(block);
        if (!anchor.hasMatch()) {
            continue;
        }
        const QString user = anchor.captured(1);
        const QString id = anchor.captured(2);
        bool duplicate = false;
        for (const DiscoveredGist &gist : found) {
            if (gist.id == id) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        const QRegularExpressionMatch file = fileNameRegex().match(block);
        QString name;
        if (file.hasMatch()) {
            name = file.captured(1);
        } else {
            // format!("gist-{user}-{}", &id[..8.min(id.len())]) —— id 全是 ASCII 十六进制。
            name = QStringLiteral("gist-%1-%2").arg(user, id.left(std::min<qsizetype>(8, id.size())));
        }
        found.push_back(DiscoveredGist{user, id, name});
    }
    return found;
}

} // namespace Collector
