#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

#include <optional>
#include <string>

#include "core/Ids.h"
#include "geo/Geo.h"
#include "ip/IpRisk.h"
#include "ip/IpRiskMessages.h"
#include "kernel/KernelManager.h"

// 搬 Rust src/geo.rs 与 src/ip_risk.rs 的 #[test]。
//
// 中文期望值一律写成 UTF-8 十六进制（tools/gen-geo-ip-cjk-codepoints.mjs 从 Rust 字面量独立
// 推导出来打印的那串 hex），手打中文在这个环境里会静默丢字符。
namespace {

QString fromHex(const char *hex)
{
    return QString::fromUtf8(QByteArray::fromHex(QByteArray(hex)));
}

const char *const kHexDatacenter = "e69cbae688bf";          // 机房
const char *const kHexResidential = "e4bd8fe5ae85";         // 住宅
const char *const kHexPublicProxy = "e585ace5bc80e4bba3e79086"; // 公开代理
const char *const kHexHalfFile = "e588a4e4b8bae58d8ae688aae69687e4bbb6"; // 判为半截文件
const char *const kHexUnusableSource = "e8bf99e4b8aae6ba90e4b88de58fafe794a8"; // 这个源不可用
const char *const kHexAlreadyReady = "e5b7b2e5b0b1e7bbaa";  // 已就绪
const char *const kHexReportJoiner = "efbc8c";              // ，
const char *const kHexNotAnIp =
    "e8bf94e59b9ee4ba86e4b88de698af20495020e79a84e58685e5aeb93a20"; // 返回了不是 IP 的内容:
const char *const kHexEgressFailedPrefix =
    "e7bb8fe4bba3e79086e58f96e587bae58fa320495020e5a4b1e8b4a53a20"; // 经代理取出口 IP 失败:
const char *const kHexNoEgressAddress =
    "e6b2a1e69c89e58fafe794a8e79a84e59b9ee698bee59cb0e59d80"; // 没有可用的回显地址
const char *const kHexVerdictDatacenter =
    "e69cbae688bf2fe4ba91e58e82e59586e587bae58fa3efbc9a414920e7ab99e782b9efbc884f70656e4149202f20476f6f676c65202f20436c61756465efbc89e69bb4e5aeb9e69893e68c892041534e20e9a38ee68ea7efbc8ce68e89e799bbe5bd95e38081e8a681e6b182e9aa8ce8af81e68896e5b081e58fb7e79a84e9a38ee999a9e5818fe9ab98";
const char *const kHexVerdictPublicProxy =
    "e4b88de698afe69cbae688bfefbc8ce4bd86e8a2abe6a087e68890e585ace5bc80e4bba3e79086202f2056504e20e587bae58fa3efbc9ae8b4a6e58fb7e7b1bbe7ab99e782b9e4bb8de4bc9ae58cbae588abe5afb9e5be85";
const char *const kHexVerdictResidential =
    "e4bd8fe5ae852f49535020e587bae58fa3efbc9ae9a38ee68ea7e58f8be5a5bdefbc8ce9809ae5b8b8e58fafe4bba5e6ada3e5b8b8e799bbe5bd95e4b88ee4bb98e8b4b9e9aa8ce8af81";
const char *const kHexArrowHeadline =
    "6e72742e6578616d706c652e6d6e20e28692203130342e31382e34322e313633"; // nrt.example.mn → 104.18.42.163

/// 写一个"看起来像 geo 库"的文件（首字节 0x0a）。
void writeGeoStub(const QString &path, int size)
{
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QByteArray(size, char(0x0a)));
    file.close();
}

QString tempDir(const QString &tag)
{
    const QString path = QDir(QDir::tempPath())
                             .absoluteFilePath(QStringLiteral("vcc-geo-%1-%2").arg(tag, Ids::newUuidV4()));
    QDir().mkpath(path);
    return path;
}

}

// ------------------------------------------------------------------ geo.rs

// 对应 Rust `truncated_or_html_bodies_are_rejected`。
TEST(Geo, TruncatedOrHtmlBodiesAreRejected)
{
    // 这次事故的原样：130,209 字节的半截库必须判坏。
    EXPECT_TRUE(Geo::validateGeoBody(QByteArray(130209, char(0x0a))).isErr());
    EXPECT_TRUE(Geo::validateGeoBody("<html>rate limited</html>").isErr());
    QByteArray junk(2000000, char('z'));
    junk[0] = 'Z';
    EXPECT_TRUE(Geo::validateGeoBody(junk).isErr());
    const QByteArray good(1500000, char(0x0a));
    const Result<quint64> validated = Geo::validateGeoBody(good);
    ASSERT_TRUE(validated.isOk()) << validated.error().toStdString();
    EXPECT_EQ(*validated, 1500000ULL);
}

TEST(Geo, ValidationErrorsCarryTheRustWording)
{
    const Result<quint64> truncated = Geo::validateGeoBody(QByteArray(130209, char(0x0a)));
    ASSERT_TRUE(truncated.isErr());
    EXPECT_TRUE(truncated.error().contains(fromHex(kHexHalfFile))) << truncated.error().toStdString();
    EXPECT_TRUE(truncated.error().contains(QStringLiteral("130209")));

    // 顺序和 Rust 一致：先卡大小，再判 HTML。所以要一个"够大但是 HTML"的响应才走到第二个分支。
    QByteArray htmlBody(1000000, char('a'));
    htmlBody[0] = '<';
    const Result<quint64> html = Geo::validateGeoBody(htmlBody);
    ASSERT_TRUE(html.isErr());
    EXPECT_TRUE(html.error().contains(fromHex(kHexUnusableSource))) << html.error().toStdString();

    QByteArray junk(1500000, char(0x0a));
    junk[0] = 'Z';
    const Result<quint64> magic = Geo::validateGeoBody(junk);
    ASSERT_TRUE(magic.isErr());
    EXPECT_TRUE(magic.error().contains(QStringLiteral("0x5a"))) << magic.error().toStdString();
}

// 对应 Rust `every_mirror_upstream_host_is_pinned_direct`：内核用自己的规则引擎决定这条
// 下载走哪儿，漏钉一家就等于把那家交回 MATCH→代理节点。
TEST(Geo, EveryMirrorUpstreamHostIsPinnedDirect)
{
    // Rust 里这三条是私有常量 UPSTREAMS；C++ 侧同样不导出，这里按 geo.rs 逐字复刻一份，
    // 只为了让"新增镜像必须登记"这条约束在测试里仍然成立。
    const QVector<QString> upstreams {
        QStringLiteral("https://gh-proxy.com/https://github.com/Loyalsoldier/v2ray-rules-dat/"
                       "releases/latest/download/"),
        QStringLiteral("https://ghfast.top/https://github.com/Loyalsoldier/v2ray-rules-dat/"
                       "releases/latest/download/"),
        QStringLiteral("https://github.com/Loyalsoldier/v2ray-rules-dat/"
                       "releases/latest/download/"),
    };
    for (const QString &upstream : upstreams) {
        QString rest = upstream.startsWith(QStringLiteral("https://"))
                           ? upstream.mid(QByteArrayLiteral("https://").size())
                           : upstream;
        const QString host = rest.split(QLatin1Char('/')).first();
        if (host == QLatin1String("github.com")) {
            continue; // 官方原链本来就该按用户规则走
        }
        EXPECT_TRUE(Geo::kMirrorDirectHosts.contains(host))
            << "mirror host " << host.toStdString()
            << " is missing from Geo::kMirrorDirectHosts";
    }
}

// 对应 Rust `readiness_uses_the_size_floor`。
TEST(Geo, ReadinessUsesTheSizeFloor)
{
    const QString dir = tempDir(QStringLiteral("ready"));
    ASSERT_FALSE(Geo::geoReady(dir));

    const QString geosite = QDir(dir).filePath(Geo::kGeoFiles.at(0));
    const QString geoip = QDir(dir).filePath(Geo::kGeoFiles.at(1));
    ASSERT_EQ(Geo::kGeoFiles.at(0).toStdString(), std::string("geosite.dat"));
    ASSERT_EQ(Geo::kGeoFiles.at(1).toStdString(), std::string("geoip.dat"));
    writeGeoStub(geosite, 130209);
    writeGeoStub(geoip, 2000000);
    // 半截的那个仍然算没就绪 —— 这正是"能起内核"和"不能起内核"的分界。
    ASSERT_FALSE(Geo::geoReady(dir));

    const QVector<Geo::FileState> state = Geo::geoState(dir);
    ASSERT_EQ(state.size(), 2);
    EXPECT_EQ(state.at(0).name.toStdString(), std::string("geosite.dat"));
    EXPECT_FALSE(state.at(0).usableBytes.has_value());
    ASSERT_TRUE(state.at(1).usableBytes.has_value());
    EXPECT_EQ(*state.at(1).usableBytes, 2000000ULL);

    writeGeoStub(geosite, 11000000);
    ASSERT_TRUE(Geo::geoReady(dir));
    QDir(dir).removeRecursively();
}

TEST(Geo, MihomoDirHangsUnderTheManagedKernelRoot)
{
    const QString expected = QDir(Kernel::managedKernelRoot()).filePath(QStringLiteral("mihomo"));
    EXPECT_EQ(Geo::mihomoDir(), expected);
    EXPECT_TRUE(Geo::mihomoDir().endsWith(QStringLiteral("mihomo")));
}

/// provision 在两个库都已就绪时完全不碰网络（Rust 侧是 `continue` 分支），这条测试因此可以安全跑。
TEST(Geo, ProvisionReportsAlreadyReadyWithoutDownloading)
{
    const QString dir = tempDir(QStringLiteral("report"));
    writeGeoStub(QDir(dir).filePath(Geo::kGeoFiles.at(0)), 11000000);
    writeGeoStub(QDir(dir).filePath(Geo::kGeoFiles.at(1)), 16000000);
    const Result<QString> report = Geo::provision(dir, QString(), false);
    ASSERT_TRUE(report.isOk()) << report.error().toStdString();
    EXPECT_TRUE(report.value().contains(fromHex(kHexAlreadyReady))) << report.value().toStdString();
    EXPECT_TRUE(report.value().contains(fromHex(kHexReportJoiner))) << report.value().toStdString();
    EXPECT_TRUE(report.value().contains(QStringLiteral("10.5MB"))) << report.value().toStdString();
    EXPECT_TRUE(report.value().contains(QStringLiteral("15.3MB"))) << report.value().toStdString();
    QDir(dir).removeRecursively();
}

/// 只读地看一眼这台机器上真装好的库，确认"首字节 0x0a + 大小下限"这套判据对真数据成立。
/// 不写不删；文件不在（换台机器）就跳过。
TEST(Geo, RealInstalledAssetsPassTheChecks)
{
    // 目录由环境变量给（VCC_REAL_ASSET_DIR）：仓库里不写死任何机器上的装机路径。
    const QString dir = QString::fromLocal8Bit(qgetenv("VCC_REAL_ASSET_DIR"));
    if (dir.isEmpty() || !QFileInfo(QDir(dir).filePath(Geo::kGeoFiles.at(0))).exists()) {
        GTEST_SKIP() << "no real assets on this machine";
    }
    EXPECT_TRUE(Geo::geoReady(dir));
    for (const QString &name : Geo::kGeoFiles) {
        QFile file(QDir(dir).filePath(name));
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        const QByteArray head = file.read(1);
        file.close();
        ASSERT_EQ(head.size(), 1);
        EXPECT_EQ(uchar(head.at(0)), 0x0a) << name.toStdString();
    }
}

// ------------------------------------------------------------------ ip_risk.rs

// 对应 Rust `hosting_org_is_flagged_as_datacenter`。
TEST(IpRisk, HostingOrgIsFlaggedAsDatacenter)
{
    const QString body = QString::fromUtf8(
        R"({"ip":"104.18.1.2","country":"US","org":"AS13335 Cloudflare, Inc.","city":"San Francisco"})");
    const Result<IpRisk::Risk> parsed = IpRisk::parse("104.18.1.2", "104.18.1.2", body, QString());
    ASSERT_TRUE(parsed.isOk()) << parsed.error().toStdString();
    const IpRisk::Risk &risk = parsed.value();
    EXPECT_TRUE(risk.datacenter);
    EXPECT_TRUE(risk.verdict.contains(fromHex(kHexDatacenter)));
    EXPECT_EQ(risk.country.toStdString(), std::string("US"));
    // server 本身就是 IP 时，摘要里不重复挂域名。
    EXPECT_FALSE(risk.headline().contains(QChar(0x2192)));
}

// 对应 Rust `isp_org_is_residential`。
TEST(IpRisk, IspOrgIsResidential)
{
    // 注意用自定义 raw 分隔符：body 里有 `)"`（TITTL 的右括号），默认的 R"(...)" 会被提前截断。
    const QString body =
        QString::fromUtf8(R"json({"country":"JP","org":"AS4713 TAIHAN INDEPENDENT TELCO TRUNK LINE (TITTL)"})json");
    const Result<IpRisk::Risk> parsed = IpRisk::parse("1.2.3.4", "1.2.3.4", body, QString());
    ASSERT_TRUE(parsed.isOk()) << parsed.error().toStdString();
    EXPECT_FALSE(parsed.value().datacenter);
    EXPECT_TRUE(parsed.value().verdict.contains(fromHex(kHexResidential)));
}

// 对应 Rust `priv_type_hosting_wins`。
TEST(IpRisk, PrivTypeHostingWins)
{
    const QString body =
        QString::fromUtf8(R"({"country":"DE","org":"AS24940 Anybody LLC","priv":"hosting"})");
    const Result<IpRisk::Risk> parsed = IpRisk::parse("5.6.7.8", "5.6.7.8", body, QString());
    ASSERT_TRUE(parsed.isOk()) << parsed.error().toStdString();
    EXPECT_TRUE(parsed.value().datacenter);
}

// 对应 Rust `resolved_domain_keeps_the_queried_hostname`。
TEST(IpRisk, ResolvedDomainKeepsTheQueriedHostname)
{
    const QString body = QString::fromUtf8(R"({"country":"US","org":"AS13335 Cloudflare, Inc."})");
    const Result<IpRisk::Risk> parsed =
        IpRisk::parse("nrt.example.mn", "104.18.42.163", body, QString());
    ASSERT_TRUE(parsed.isOk()) << parsed.error().toStdString();
    EXPECT_EQ(parsed.value().hostname.toStdString(), std::string("nrt.example.mn"));
    const QString headline = parsed.value().headline();
    EXPECT_TRUE(headline.startsWith(fromHex(kHexArrowHeadline))) << headline.toStdString();
}

// 对应 Rust `ip_api_hosting_flag_rescues_keyword_misses`。
TEST(IpRisk, IpApiHostingFlagRescuesKeywordMisses)
{
    const QString body = QString::fromUtf8(R"({"country":"JP","org":"AS12345 Widget Telecom KK"})");
    const Result<IpRisk::Risk> alone = IpRisk::parse("9.9.9.9", "9.9.9.9", body, QString());
    ASSERT_TRUE(alone.isOk()) << alone.error().toStdString();
    EXPECT_FALSE(alone.value().datacenter);

    const QString api = QString::fromUtf8(R"({"status":"success","hosting":true,"proxy":false,"mobile":false})");
    const Result<IpRisk::Risk> rescued = IpRisk::parse("9.9.9.9", "9.9.9.9", body, api);
    ASSERT_TRUE(rescued.isOk()) << rescued.error().toStdString();
    EXPECT_TRUE(rescued.value().datacenter);
    EXPECT_FALSE(rescued.value().publicProxy);
}

// 对应 Rust `public_proxy_without_hosting_gets_its_own_verdict`。
TEST(IpRisk, PublicProxyWithoutHostingGetsItsOwnVerdict)
{
    const QString body =
        QString::fromUtf8(R"({"country":"JP","org":"AS2527 Sony Network Communications Inc."})");
    const QString api = QString::fromUtf8(R"({"status":"success","hosting":false,"proxy":true,"mobile":false})");
    const Result<IpRisk::Risk> parsed =
        IpRisk::parse("182.170.241.155", "182.170.241.155", body, api);
    ASSERT_TRUE(parsed.isOk()) << parsed.error().toStdString();
    EXPECT_FALSE(parsed.value().datacenter);
    EXPECT_TRUE(parsed.value().publicProxy);
    EXPECT_TRUE(parsed.value().verdict.contains(fromHex(kHexPublicProxy)))
        << parsed.value().verdict.toStdString();
}

// 对应 Rust `broken_api_body_falls_back_to_ipinfo_only`。
TEST(IpRisk, BrokenApiBodyFallsBackToIpinfoOnly)
{
    const QString body = QString::fromUtf8(R"({"country":"KR","org":"AS9848 Korea Telecom"})");
    const Result<IpRisk::Risk> parsed =
        IpRisk::parse("1.1.1.1", "1.1.1.1", body, QStringLiteral("<html>429</html>"));
    ASSERT_TRUE(parsed.isOk()) << parsed.error().toStdString();
    EXPECT_FALSE(parsed.value().datacenter);
    EXPECT_FALSE(parsed.value().publicProxy);
}

TEST(IpRisk, UnparsableIpinfoBodyIsAnError)
{
    const Result<IpRisk::Risk> parsed =
        IpRisk::parse("1.1.1.1", "1.1.1.1", QStringLiteral("<html>429</html>"), QString());
    EXPECT_TRUE(parsed.isErr());
}

// ------------------------------------------------------------------ ip_risk.rs 的纯函数面

TEST(IpRisk, QueryUrlsMatchRustByteForByte)
{
    EXPECT_EQ(IpRisk::ipinfoUrl("1.2.3.4").toStdString(),
              std::string("https://ipinfo.io/1.2.3.4/json"));
    EXPECT_EQ(IpRisk::ipApiUrl("1.2.3.4").toStdString(),
              std::string("http://ip-api.com/json/1.2.3.4?fields=status,hosting,proxy,mobile,as,isp"));
    ASSERT_EQ(IpRisk::egressEchoUrls().size(), 2);
    EXPECT_EQ(IpRisk::egressEchoUrls().at(0).toStdString(), std::string("https://api.ipify.org"));
    EXPECT_EQ(IpRisk::egressEchoUrls().at(1).toStdString(), std::string("https://ipinfo.io/ip"));
}

TEST(IpRisk, AddressTextFollowsRustsIpLexing)
{
    EXPECT_EQ(IpRisk::addressText("1.2.3.4").toStdString(), std::string("1.2.3.4"));
    EXPECT_TRUE(IpRisk::addressText("01.2.3.4").isNull());
    EXPECT_TRUE(IpRisk::addressText("1.2.3").isNull());
    EXPECT_TRUE(IpRisk::addressText("256.1.1.1").isNull());
    EXPECT_TRUE(IpRisk::addressText("nrt.example.mn").isNull());
    EXPECT_EQ(IpRisk::addressText("::1").toStdString(), std::string("::1"));
    EXPECT_EQ(IpRisk::addressText("2001:0DB8:0000:0000:0000:0000:0000:0001").toStdString(),
              std::string("2001:db8::1"));
    EXPECT_EQ(IpRisk::addressText("2001:db8:0:0:1:0:0:1").toStdString(), std::string("2001:db8::1:0:0:1"));
    EXPECT_EQ(IpRisk::addressText("1::").toStdString(), std::string("1::"));
}

TEST(IpRisk, EgressCandidateTakesTheFirstWhitespaceWord)
{
    const IpRisk::EgressCandidate ip = IpRisk::egressCandidate("  203.0.113.9 \n");
    EXPECT_EQ(ip.text.toStdString(), std::string("203.0.113.9"));
    EXPECT_EQ(ip.addressText.toStdString(), std::string("203.0.113.9"));

    const IpRisk::EgressCandidate junk = IpRisk::egressCandidate("not an ip");
    EXPECT_EQ(junk.text.toStdString(), std::string("not"));
    EXPECT_TRUE(junk.addressText.isNull());

    const IpRisk::EgressCandidate empty = IpRisk::egressCandidate("   ");
    EXPECT_TRUE(empty.text.isEmpty());
    EXPECT_TRUE(empty.addressText.isNull());
}

TEST(IpRisk, ProxyWithoutSchemeFailsBeforeAnyRequest)
{
    EXPECT_TRUE(IpRisk::checkProxyUrl(QStringLiteral("http://127.0.0.1:7890")).isOk());
    EXPECT_TRUE(IpRisk::checkProxyUrl(QStringLiteral("socks5://127.0.0.1:7891")).isOk());
    const Status bad = IpRisk::checkProxyUrl(QStringLiteral("127.0.0.1:7890"));
    ASSERT_TRUE(bad.isErr());
    EXPECT_TRUE(bad.error().contains(fromHex("e4bba3e79086e59cb0e59d80e697a0e695883a20")))
        << bad.error().toStdString(); // 代理地址无效:
}

// ------------------------------------------------------------------ ip_risk.rs 的编排（注入取文本，不发请求）

namespace {

struct Call
{
    QString url;
    QString what;
    QString proxy;
};

/// 一台假 IP 情报源：ipinfo 返回 `ipinfoBody`，ip-api 按 `apiError` 决定失败与否。
IpRisk::Fetcher textSource(const QString &ipinfoBody, const QString &apiBody,
                           const QString &apiError, QVector<Call> *calls)
{
    return [ipinfoBody, apiBody, apiError, calls](const QString &url, const QString &what,
                                                 const QString &proxy) {
        if (calls) {
            calls->push_back({url, what, proxy});
        }
        if (what == IpRisk::whatIpinfo()) {
            return Result<QString>::withValue(ipinfoBody);
        }
        if (what == IpRisk::whatIpApi()) {
            return apiError.isNull() ? Result<QString>::withValue(apiBody)
                                    : Result<QString>::withError(apiError);
        }
        return Result<QString>::withError(QStringLiteral("unexpected url: %1").arg(url));
    };
}

}

TEST(IpRisk, ClassifyIpAsksBothSourcesDirectlyAndSurvivesIpApiFailure)
{
    QVector<Call> calls;
    const IpRisk::Fetcher fetch = textSource(
        QString::fromUtf8(R"({"country":"JP","org":"AS12345 Widget Telecom KK"})"), QString(),
        QStringLiteral("connection refused"), &calls);
    const Result<IpRisk::Risk> parsed = IpRisk::classifyIpWith(fetch, "nrt.example.mn", "9.9.9.9");
    ASSERT_TRUE(parsed.isOk()) << parsed.error().toStdString();
    EXPECT_FALSE(parsed.value().datacenter);
    ASSERT_EQ(calls.size(), 2);
    EXPECT_EQ(calls.at(0).url.toStdString(), std::string("https://ipinfo.io/9.9.9.9/json"));
    EXPECT_EQ(calls.at(1).url.toStdString(),
              std::string("http://ip-api.com/json/9.9.9.9?fields=status,hosting,proxy,mobile,as,isp"));
    EXPECT_TRUE(calls.at(0).proxy.isNull());
    EXPECT_TRUE(calls.at(1).proxy.isNull());
    EXPECT_EQ(calls.at(0).what.toStdString(), std::string("ipinfo"));
    EXPECT_EQ(calls.at(1).what.toStdString(), std::string("ip-api"));
}

TEST(IpRisk, IpinfoFailureIsTheWholeVerdict)
{
    QVector<Call> calls;
    const IpRisk::Fetcher fetch = [&calls](const QString &url, const QString &what,
                                          const QString &proxy) {
        calls.push_back({url, what, proxy});
        return Result<QString>::withError(QStringLiteral("nope"));
    };
    const Result<IpRisk::Risk> parsed = IpRisk::classifyIpWith(fetch, "host", "9.9.9.9");
    ASSERT_TRUE(parsed.isErr());
    // Rust 侧 classify_ip 对 fetch_text 用的是 `?`：文案是 fetch_text 里拼好的，往上原样抛。
    EXPECT_EQ(parsed.error().toStdString(), std::string("nope"));
    // ipinfo 一路就定了失败，ip-api 连问都不问。
    ASSERT_EQ(calls.size(), 1);
    EXPECT_EQ(calls.at(0).what.toStdString(), std::string("ipinfo"));
}

TEST(IpRisk, LookupSkipsDnsWhenTheServerIsAlreadyAnIp)
{
    QVector<Call> calls;
    const IpRisk::Fetcher fetch = textSource(QString::fromUtf8(R"({"country":"US","org":"AS13335 Cloudflare, Inc."})"),
                                            QString(), QString(), &calls);
    bool resolverRan = false;
    const IpRisk::Resolver resolve = [&resolverRan](const QString &) {
        resolverRan = true;
        return Result<QString>::withError(QStringLiteral("should not run"));
    };
    const Result<IpRisk::Risk> parsed = IpRisk::lookupWith(fetch, resolve, "104.18.1.2");
    ASSERT_TRUE(parsed.isOk()) << parsed.error().toStdString();
    EXPECT_FALSE(resolverRan);
    EXPECT_TRUE(parsed.value().datacenter);
    EXPECT_EQ(parsed.value().ip.toStdString(), std::string("104.18.1.2"));
    EXPECT_EQ(parsed.value().hostname.toStdString(), std::string("104.18.1.2"));
}

TEST(IpRisk, LookupResolvesDomainsAndKeepsTheHostname)
{
    QVector<Call> calls;
    const IpRisk::Fetcher fetch = textSource(QString::fromUtf8(R"({"country":"US","org":"AS13335 Cloudflare, Inc."})"),
                                            QString(), QString(), &calls);
    const IpRisk::Resolver resolve = [](const QString &host) {
        return Result<QString>::withValue(host == QLatin1String("nrt.example.mn")
                                              ? QStringLiteral("104.18.42.163")
                                              : QStringLiteral("2001:db8::1"));
    };
    const Result<IpRisk::Risk> parsed = IpRisk::lookupWith(fetch, resolve, "nrt.example.mn");
    ASSERT_TRUE(parsed.isOk()) << parsed.error().toStdString();
    EXPECT_EQ(parsed.value().hostname.toStdString(), std::string("nrt.example.mn"));
    EXPECT_EQ(parsed.value().ip.toStdString(), std::string("104.18.42.163"));
    ASSERT_FALSE(calls.isEmpty());
    EXPECT_EQ(calls.at(0).url.toStdString(), std::string("https://ipinfo.io/104.18.42.163/json"));
}

TEST(IpRisk, LookupPropagatesTheResolveErrorVerbatim)
{
    const IpRisk::Fetcher fetch = textSource(QStringLiteral("{}"), QString(), QString(), nullptr);
    const IpRisk::Resolver resolve = [](const QString &) {
        return Result<QString>::withError(QStringLiteral("Name or service not known"));
    };
    const Result<IpRisk::Risk> parsed = IpRisk::lookupWith(fetch, resolve, "broken.example.mn");
    ASSERT_TRUE(parsed.isErr());
    // 对应 Rust `lookup`：`resolve_first(server).await?` —— 包装文案在 resolve_first 里就拼好了，
    // lookup 只是原样往上抛，且一次请求都不发。
    EXPECT_EQ(parsed.error().toStdString(), std::string("Name or service not known"));
}

/// 用户可见文案的期望值直接从 Rust 字面量的 UTF-8 字节比对（tools/gen-geo-ip-cjk-codepoints.mjs
/// 独立推导出来的第二遍），这几条模板只有走 DNS/HTTP 才会被拼出来，测试里点不到那条代码。
TEST(IpRisk, MessageTemplatesMatchRust)
{
    EXPECT_EQ(IpRisk::Messages::resolveFailed(),
              fromHex("e8a7a3e69e90e88a82e782b9e59f9fe5908d" "207b686f73747d20"
                      "e5a4b1e8b4a5" "3a207b6572726f727d"));
    EXPECT_EQ(IpRisk::Messages::noRecords(),
              fromHex("e88a82e782b9e59f9fe5908d" "207b686f73747d20"
                      "e6b2a1e69c89e58fafe794a8e8a7a3e69e90e8aeb0e5bd95"));
    EXPECT_EQ(IpRisk::Messages::queryFailed(),
              fromHex("7b776861747d20e69fa5e8afa2e5a4b1e8b4a53a207b6572726f723a237d"));
    EXPECT_EQ(IpRisk::Messages::readFailed(),
              fromHex("7b776861747d20e8afbbe58f96e5938de5ba94e5a4b1e8b4a53a207b6572726f723a237d"));
    EXPECT_EQ(IpRisk::Messages::noEgressAddress(), fromHex(kHexNoEgressAddress));
    EXPECT_EQ(IpRisk::Messages::egressFailed(),
              fromHex("e7bb8fe4bba3e79086e58f96e587bae58fa3" "20495020e5a4b1e8b4a5"
                      "3a207b6c6173745f6572726f727d"));
    EXPECT_EQ(IpRisk::Messages::notAnIp(),
              fromHex("7b75726c7d20e8bf94e59b9ee4ba86e4b88de698af" "20495020e79a84e58685e5aeb9"
                      "3a20" "7b63616e6469646174653a3f7d"));
    // verdict 三段与摘要里那四个字段。
    EXPECT_EQ(IpRisk::Messages::verdictDatacenter(), fromHex(kHexVerdictDatacenter));
    EXPECT_EQ(IpRisk::Messages::verdictPublicProxy(), fromHex(kHexVerdictPublicProxy));
    EXPECT_EQ(IpRisk::Messages::verdictResidential(), fromHex(kHexVerdictResidential));
    EXPECT_EQ(IpRisk::Messages::kindDatacenter(), fromHex("e69cbae688bf2fe4ba91"));
    EXPECT_EQ(IpRisk::Messages::kindResidential(), fromHex("e4bd8fe5ae852f4953503f"));
    EXPECT_EQ(IpRisk::Messages::unknownRegion(), fromHex("e69caae79fa5e59cb0e58cba"));
    EXPECT_EQ(IpRisk::Messages::noAsnInfo(), fromHex("e697a02041534e20e4bfa1e681af"));
    EXPECT_EQ(IpRisk::Messages::headlineJoiner(), fromHex("20c2b720"));
}

TEST(IpRisk, EgressUsesTheProxiedEchoThenClassifiesDirectly)
{
    QVector<Call> calls;
    const QString proxy = QStringLiteral("http://127.0.0.1:7890");
    const IpRisk::Fetcher fetch = [&calls, proxy](const QString &url, const QString &what,
                                                  const QString &hostProxy) {
        calls.push_back({url, what, hostProxy});
        if (url == QStringLiteral("https://api.ipify.org")) {
            return Result<QString>::withValue(QStringLiteral(" 203.0.113.9 \n"));
        }
        if (url == QStringLiteral("https://ipinfo.io/ip")) {
            return Result<QString>::withError(QStringLiteral("first echo is down"));
        }
        if (what == IpRisk::whatIpinfo()) {
            return Result<QString>::withValue(
                QString::fromUtf8(R"({"country":"US","org":"AS20473 Choopa, LLC"})"));
        }
        return Result<QString>::withError(QStringLiteral("ip-api down"));
    };
    const Result<IpRisk::Risk> parsed = IpRisk::lookupEgressWith(fetch, proxy);
    ASSERT_TRUE(parsed.isOk()) << parsed.error().toStdString();
    EXPECT_TRUE(parsed.value().datacenter); // Choopa 在关键字表里
    EXPECT_EQ(parsed.value().ip.toStdString(), std::string("203.0.113.9"));
    EXPECT_EQ(parsed.value().hostname.toStdString(), std::string("203.0.113.9"));
    // 第一个回显就答了，第二个不该再问；归属查询走直连。
    ASSERT_EQ(calls.size(), 3);
    EXPECT_EQ(calls.at(0).url.toStdString(), std::string("https://api.ipify.org"));
    EXPECT_EQ(calls.at(0).proxy.toStdString(), proxy.toStdString());
    EXPECT_EQ(calls.at(1).url.toStdString(), std::string("https://ipinfo.io/203.0.113.9/json"));
    EXPECT_TRUE(calls.at(1).proxy.isNull());
    EXPECT_EQ(calls.at(2).url.toStdString(),
              std::string("http://ip-api.com/json/203.0.113.9?fields=status,hosting,proxy,mobile,as,isp"));
    EXPECT_TRUE(calls.at(2).proxy.isNull());
}

TEST(IpRisk, EgressTriesBothEchoServicesAndReportsTheLastFailure)
{
    const IpRisk::Fetcher fetch = [](const QString &url, const QString &, const QString &) {
        return Result<QString>::withError(url == QStringLiteral("https://api.ipify.org")
                                              ? QStringLiteral("echo one failed")
                                              : QStringLiteral("echo two failed"));
    };
    const Result<IpRisk::Risk> parsed =
        IpRisk::lookupEgressWith(fetch, QStringLiteral("http://127.0.0.1:7890"));
    ASSERT_TRUE(parsed.isErr());
    EXPECT_EQ(parsed.error().toStdString(),
              (fromHex(kHexEgressFailedPrefix) + QStringLiteral("echo two failed")).toStdString());
}

TEST(IpRisk, EgressRejectsNonIpEchoBodies)
{
    const IpRisk::Fetcher fetch = [](const QString &, const QString &, const QString &) {
        return Result<QString>::withValue(QStringLiteral("oops"));
    };
    const Result<IpRisk::Risk> parsed =
        IpRisk::lookupEgressWith(fetch, QStringLiteral("http://127.0.0.1:7890"));
    ASSERT_TRUE(parsed.isErr());
    EXPECT_TRUE(parsed.error().contains(fromHex(kHexEgressFailedPrefix)))
        << parsed.error().toStdString();
    // 第二个回显地址 + Rust 的 {:?}（带引号）。
    EXPECT_TRUE(parsed.error().contains(QStringLiteral("https://ipinfo.io/ip")))
        << parsed.error().toStdString();
    EXPECT_TRUE(parsed.error().contains(fromHex(kHexNotAnIp))) << parsed.error().toStdString();
    EXPECT_TRUE(parsed.error().endsWith(QStringLiteral("\"oops\""))) << parsed.error().toStdString();
}

TEST(IpRisk, EgressStopsAtTheFirstEchoThatAnswers)
{
    QVector<Call> calls;
    const QString proxy = QStringLiteral("http://127.0.0.1:7890");
    const IpRisk::Fetcher fetch = [&calls, proxy](const QString &url, const QString &what,
                                                 const QString &hostProxy) {
        calls.push_back({url, what, hostProxy});
        if (url == QStringLiteral("https://api.ipify.org")) {
            return Result<QString>::withValue(QStringLiteral("198.51.100.7"));
        }
        if (what == IpRisk::whatIpinfo()) {
            return Result<QString>::withValue(QString::fromUtf8(R"({"country":"NL","org":"AS60781 LEASEWEB"})"));
        }
        return Result<QString>::withValue(QString::fromUtf8(R"({"hosting":true,"proxy":false})"));
    };
    const Result<IpRisk::Risk> parsed = IpRisk::lookupEgressWith(fetch, proxy);
    ASSERT_TRUE(parsed.isOk()) << parsed.error().toStdString();
    EXPECT_TRUE(parsed.value().datacenter);
    ASSERT_EQ(calls.size(), 3);
    for (const Call &call : calls) {
        EXPECT_NE(call.url.toStdString(), std::string("https://ipinfo.io/ip"))
            << "第二个回显地址不该被用到";
    }
    EXPECT_EQ(calls.at(1).proxy.toStdString(), std::string());
    EXPECT_EQ(calls.at(2).proxy.toStdString(), std::string());
}

