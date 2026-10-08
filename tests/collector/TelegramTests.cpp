#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QHash>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QtGlobal>
#include <QVector>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <optional>

#include "collector/Telegram.h"
#include "collector/TelegramCjkGenerated.h"
#include "core/Crypto.h"
#include "db/Database.h"
#include "models/Node.h"
#include "models/Source.h"
#include "models/Types.h"
#include "net/HttpClient.h"
#include "parser/Subscription.h"
#include "sysproxy/SystemProxy.h"

// 对应 vpn-parser/src/collector/telegram.rs 的 #[cfg(test)] mod tests：
// 6 个普通 #[test] 逐条移植（名字保持 Rust 原样），3 个 #[ignore] 的网络探针移植成
// DISABLED_ 开头 —— gtest 里等价于 cargo 的 ignored 档，默认不跑，要跑得
// --gtest_also_run_disabled_tests 且这台机器得有一个能出 t.me 的本地代理。
// 另外补 6 个离线用例：Rust 那 3 个 ignored 用例覆盖的翻页 / 304 / 内容未变 /
// 只增不删 / 无出口 / 订阅链接失败记账，这里用注入的假出口在不碰网络的前提下测出来。
//
// 断言消息里的中文全部来自 tools/gen-telegram-cjk.mjs 生成的 TelegramCjkGenerated.h
// （逐字取自 telegram.rs 的测试模块），不许在这里手打中文。
namespace {

namespace Tg = Collector::Telegram;
namespace Text = Collector::Telegram::TestText;

const char *kFirstPage = "https://t.me/s/demo";
const char *kSecondPage = "https://t.me/s/demo?before=198";
const char *kSubUrl =
    "https://nodebuf.com/files/public/cmuxccknp8l36oh5wl6t8s964/download";

/// 每个用例一套临时库 + 一条 Telegram 源。
struct Fixture {
    QTemporaryDir directory;
    std::shared_ptr<Database> db;
    Models::Source source;
};

Result<Fixture> makeFixture(const QString &urlTemplate, const QString &name)
{
    Fixture fixture;
    if (!fixture.directory.isValid()) {
        return Result<Fixture>::withError(QStringLiteral("temp dir failed"));
    }
    const QString path = QDir(fixture.directory.path()).filePath(QStringLiteral("vpn-parser.db"));
    Result<std::shared_ptr<Database>> opened = Database::connect(path);
    if (opened.isErr()) {
        return Result<Fixture>::withError(opened.error());
    }
    fixture.db = opened.takeValue();
    Models::CreateSource input;
    input.name = name;
    input.urlTemplate = urlTemplate;
    input.enabled = true;
    input.remark = QString();
    Result<Models::Source> created = fixture.db->createSource(input);
    if (created.isErr()) {
        return Result<Fixture>::withError(created.error());
    }
    fixture.source = created.takeValue();
    return Result<Fixture>::withValue(std::move(fixture));
}

/// 假出口：URL 精确命中就给页面，命中 notModified 就回 304，其余一律失败。
/// 对应 Rust 的 download_candidate，把网络这一层完全隔开。
struct FakeTransport {
    QHash<QString, QString> bodies;
    QStringList notModified;
    QStringList requested;
    QStringList phases;
    QString errorMessage = QStringLiteral("request failed: refused by fixture");
    int nodeReports = 0;
    int reportedNodes = 0;

    Tg::PageFetcher fetcher()
    {
        return [this](const Models::Source &, const QString &url, const QString &,
                      std::chrono::milliseconds) -> Result<std::optional<Tg::Downloaded>> {
            requested.append(url);
            if (bodies.contains(url)) {
                Tg::Downloaded downloaded;
                downloaded.body = bodies.value(url).toUtf8();
                downloaded.finalUrl = url;
                return Result<std::optional<Tg::Downloaded>>::withValue(
                    std::optional<Tg::Downloaded>(downloaded));
            }
            if (notModified.contains(url)) {
                return Result<std::optional<Tg::Downloaded>>::withValue(std::nullopt);
            }
            return Result<std::optional<Tg::Downloaded>>::withError(errorMessage);
        };
    }

    Tg::PhaseReporter phase()
    {
        return [this](const QString &, const QString &, Tg::TaskPhase phaseValue) {
            phases.append(QString::number(int(phaseValue)));
        };
    }

    Tg::NodesReporter nodes()
    {
        return [this](const QString &, const QString &, int nodeCount) {
            nodeReports += 1;
            reportedNodes = nodeCount;
        };
    }
};

struct Arg {
    const char *marker;
    QString value;
};

/// 与 Rust 的 format! 同构：`{name}` 按名字换，`{}` 按顺序顶替，认不出的原样留着。
QString render(const QString &text, std::initializer_list<Arg> args)
{
    const QVector<Arg> named(args.begin(), args.end());
    QStringList positional;
    for (const Arg &arg : named) {
        if (std::strcmp(arg.marker, "{}") == 0) {
            positional.append(arg.value);
        }
    }
    QString out;
    qsizetype next = 0;
    for (qsizetype index = 0; index < text.size();) {
        if (text.at(index) == QLatin1Char('{')) {
            const qsizetype close = text.indexOf(QLatin1Char('}'), index + 1);
            if (close > index) {
                const QString name = text.mid(index + 1, close - index - 1);
                if (name.isEmpty()) {
                    if (next < positional.size()) {
                        out += positional.at(next);
                        next += 1;
                        index = close + 1;
                        continue;
                    }
                } else {
                    bool consumed = false;
                    // args 里的具名占位符带着花括号，模板里读到的名字不带，按带花括号的形式比。
                    const QString braced = QLatin1Char('{') + name + QLatin1Char('}');
                    for (const Arg &arg : named) {
                        if (braced == QString::fromLatin1(arg.marker)) {
                            out += arg.value;
                            index = close + 1;
                            consumed = true;
                            break;
                        }
                    }
                    if (consumed) {
                        continue;
                    }
                }
            }
        }
        out += text.at(index);
        index += 1;
    }
    return out;
}

QString debugList(const QStringList &values)
{
    QStringList quoted;
    for (const QString &value : values) {
        quoted.append(QStringLiteral("\"%1\"").arg(value));
    }
    return QStringLiteral("[%1]").arg(quoted.join(QStringLiteral(", ")));
}

/// Rust 的 `assert_eq!(got.as_deref(), Some(want))`：None 也要能和 Some 比出来。
QString optionalText(const QString &value)
{
    return value.isNull() ? QStringLiteral("None") : value;
}

// ---- 6 个普通 #[test] ----

TEST(Telegram, channel_name_accepts_preview_page_plain_link_and_handle)
{
    const QStringList urls = {
        QStringLiteral("https://t.me/s/v2node"),
        QStringLiteral("http://t.me/s/v2node"),
        QStringLiteral("t.me/s/v2node"),
        QStringLiteral("https://t.me/v2node"),
        QStringLiteral("https://t.me/v2node/12588"),
        QStringLiteral("@v2node"),
        QStringLiteral("https://telegram.me/s/V2freeNode"),
    };
    for (const QString &url : urls) {
        const QString want = url.contains(QLatin1String("V2freeNode"))
            ? QStringLiteral("V2freeNode")
            : QStringLiteral("v2node");
        EXPECT_EQ(optionalText(Tg::channelName(url)).toStdString(), want.toStdString())
            << render(Text::line600(), {{"{url}", url}, {"{want}", want}}).toStdString();
    }
}

TEST(Telegram, channel_name_rejects_private_and_non_channel_links)
{
    const QStringList urls = {
        QStringLiteral("https://t.me/+AbCdEfGhIjKlMnOp"),
        QStringLiteral("https://t.me/joinchat/AAAAAEag"),
        QStringLiteral("https://t.me/proxy?server=1.2.3.4"),
        QStringLiteral("https://t.me/addstickers/whatever"),
        QStringLiteral("https://github.com/some/repo/raw/main/sub.txt"),
        QStringLiteral("https://example.com/s/v2node"),
        QStringLiteral("not a url at all"),
        QString(),
        QStringLiteral("@ab"),
        QStringLiteral("@has space"),
    };
    for (const QString &url : urls) {
        EXPECT_TRUE(Tg::channelName(url).isNull())
            << render(Text::line618(), {{"{url}", url}}).toStdString();
    }
}

TEST(Telegram, message_blocks_skip_forwarded_quote_and_ids_are_ascending)
{
    const QString html = Text::fixtureHtml();
    const QStringList blocks = Tg::messageBlocks(html);
    ASSERT_EQ(blocks.size(), 2) << Text::line626().toStdString();
    EXPECT_FALSE(blocks.join(QString()).contains(QLatin1String("elsewhere")))
        << Text::line629().toStdString();
    const QVector<qulonglong> want = {198, 200};
    EXPECT_EQ(Tg::messageIds(html), want);
}

TEST(Telegram, payloads_split_raw_uris_from_subscription_urls)
{
    const Tg::Payloads found = Tg::payloads(Tg::messageBlocks(Text::fixtureHtml()));
    ASSERT_EQ(found.uris.size(), 4)
        << render(Text::line637(), {{"{uris:?}", debugList(found.uris)}}).toStdString();
    EXPECT_TRUE(std::any_of(found.uris.begin(), found.uris.end(), [](const QString &value) {
        return value.startsWith(QLatin1String("ss://"));
    }));
    EXPECT_TRUE(std::any_of(found.uris.begin(), found.uris.end(), [](const QString &value) {
        return value.startsWith(QLatin1String("vless://"));
    }));
    EXPECT_TRUE(std::any_of(found.uris.begin(), found.uris.end(), [](const QString &value) {
        return value.startsWith(QLatin1String("trojan://"));
    }));
    const QStringList want{QString::fromLatin1(kSubUrl)};
    EXPECT_EQ(found.urls.join(QLatin1Char('\n')).toStdString(),
              want.join(QLatin1Char('\n')).toStdString())
        << Text::line644().toStdString();
}

TEST(Telegram, raw_uris_parse_into_nodes)
{
    const Tg::Payloads found = Tg::payloads(Tg::messageBlocks(Text::fixtureHtml()));
    const QVector<Models::Node> nodes =
        Parser::parseSubscription(Tg::rawUriText(found.uris), QStringLiteral("demo"));
    ASSERT_EQ(nodes.size(), 4)
        << render(Text::line652(), {{"{uris:?}", debugList(found.uris)}}).toStdString();
    EXPECT_TRUE(std::any_of(nodes.begin(), nodes.end(),
                            [](const Models::Node &node) { return node.port == 8389; }));
    EXPECT_TRUE(std::any_of(nodes.begin(), nodes.end(), [](const Models::Node &node) {
        return node.server == QLatin1String("45.76.12.3");
    }));
}

TEST(Telegram, entities_are_decoded_without_swallowing_angle_brackets)
{
    EXPECT_EQ(
        Tg::plainText(
            QStringLiteral("a &quot;ss://x@y:443&quot; &amp; b &lt;b&gt;c&lt;/b&gt;<br/>d"))
            .toStdString(),
        std::string("a \"ss://x@y:443\" & b <b>c</b>\nd"));
}

// ---- 3 个 #[ignore] 的网络探针（默认不跑）----

/// 用 Net::send 顶替 Collector::download_candidate：只补出口，不含并发闸与镜像赛跑。
Tg::PageFetcher makeHttpFetcher(const QString &proxyUrl)
{
    return [proxyUrl](const Models::Source &source, const QString &url, const QString &,
                      std::chrono::milliseconds budget) -> Result<std::optional<Tg::Downloaded>> {
        Q_UNUSED(source);
        Net::Request request;
        request.url = url;
        request.proxyUrl = proxyUrl;
        request.userAgent = QStringLiteral("vpn-parser/1.0 (+https://localhost)");
        request.timeoutMs = int(std::max<qint64>(budget.count(), 500));
        const Result<Net::Response> sent = Net::send(request);
        if (sent.isErr()) {
            return Result<std::optional<Tg::Downloaded>>::withError(sent.error());
        }
        const Net::Response &response = sent.value();
        if (response.status == 304) {
            return Result<std::optional<Tg::Downloaded>>::withValue(std::nullopt);
        }
        if (!response.isSuccess()) {
            return Result<std::optional<Tg::Downloaded>>::withError(
                QStringLiteral("HTTP %1").arg(response.status));
        }
        Tg::Downloaded downloaded;
        downloaded.body = response.body;
        downloaded.finalUrl = response.finalUrl.isEmpty() ? url : response.finalUrl;
        downloaded.etag = response.headerValue(QStringLiteral("ETag"));
        downloaded.lastModified = response.headerValue(QStringLiteral("Last-Modified"));
        return Result<std::optional<Tg::Downloaded>>::withValue(
            std::optional<Tg::Downloaded>(downloaded));
    };
}

TEST(Telegram, DISABLED_real_network_telegram_channel_yields_nodes)
{
    QString proxy = QString::fromLocal8Bit(qgetenv("VPN_PARSER_PROBE_PROXY"));
    if (proxy.isEmpty()) {
        proxy = QStringLiteral("http://127.0.0.1:9980");
    }
    QString html;
    // 这条链路上境外握手会被间歇掐断，探针本身就重试几次，别把出口的抖动报成解析坏了。
    for (int attempt = 0; attempt < 3; attempt += 1) {
        Net::Request request;
        request.url = QStringLiteral("https://t.me/s/v2node");
        request.proxyUrl = proxy;
        request.userAgent = QStringLiteral("vpn-parser/1.0");
        request.timeoutMs = 20000;
        const Result<Net::Response> sent = Net::send(request);
        if (sent.isOk() && sent.value().isSuccess()) {
            html = QString::fromUtf8(sent.value().body);
            break;
        }
        const bool readBodyFailed = sent.isOk();
        const QString detail =
            readBodyFailed ? QStringLiteral("HTTP %1").arg(sent.value().status) : sent.error();
        GTEST_LOG_(INFO) << render(readBodyFailed ? Text::line689() : Text::line687(),
                                   {{"{}", QString::number(attempt + 1)},
                                    {"{error:#}", detail}})
                                .toStdString();
        QThread::msleep(2000);
    }
    ASSERT_FALSE(html.isEmpty()) << Text::line693().toStdString();
    const QStringList blocks = Tg::messageBlocks(html);
    ASSERT_GT(blocks.size(), 5)
        << render(Text::line697(), {{"{:#?}", QString::number(blocks.size())}}).toStdString();
    const Tg::Payloads found = Tg::payloads(blocks);
    ASSERT_TRUE(!found.uris.isEmpty() || !found.urls.isEmpty()) << Text::line703().toStdString();
}

TEST(Telegram, DISABLED_real_network_telegram_falls_back_to_system_proxy)
{
    // 故意用不带代理的那一份 collector（via_proxy=false），让它自己去查系统代理。
    const QString systemProxy = SysProxy::currentSystemProxyUrl();
    if (systemProxy.isNull()) {
        GTEST_SKIP() << Text::line718().toStdString();
    }
    Result<Fixture> opened = makeFixture(QStringLiteral("@v2node"), Text::line731());
    ASSERT_TRUE(opened.isOk()) << opened.error().toStdString();
    Fixture fixture = opened.takeValue();

    Tg::TelegramContext context;
    context.db = fixture.db;
    context.viaProxy = false;
    context.systemProxyUrl = [] { return SysProxy::currentSystemProxyUrl(); };
    context.applyProxy = [](const QString &proxyUrl) -> Result<Tg::PageFetcher> {
        return Result<Tg::PageFetcher>::withValue(makeHttpFetcher(proxyUrl));
    };

    const Models::FetchResult result =
        Tg::fetchTelegramSource(context, fixture.source,
                                Tg::channelName(fixture.source.urlTemplate),
                                QStringLiteral("task-sys"), std::chrono::seconds(120));
    GTEST_LOG_(INFO)
        << render(Text::line741(),
                  {{"{system_proxy}", systemProxy},
                   {"{}", result.success ? QStringLiteral("true") : QStringLiteral("false")},
                   {"{}", QString::number(result.nodeCount)},
                   {"{}", result.message}})
               .toStdString();
    EXPECT_TRUE(result.message.contains(Text::line745()))
        << render(Text::line746(), {{"{}", result.message}}).toStdString();
    EXPECT_TRUE(result.success) << result.message.toStdString();
    EXPECT_GT(result.nodeCount, 0);
}

TEST(Telegram, DISABLED_real_network_telegram_source_fetches_and_accumulates)
{
    QString proxy = QString::fromLocal8Bit(qgetenv("VPN_PARSER_PROBE_PROXY"));
    if (proxy.isEmpty()) {
        proxy = QStringLiteral("http://127.0.0.1:9980");
    }
    Result<Fixture> opened =
        makeFixture(QStringLiteral("https://t.me/s/v2node"), QStringLiteral("TG v2node"));
    ASSERT_TRUE(opened.isOk()) << opened.error().toStdString();
    Fixture fixture = opened.takeValue();

    Tg::TelegramContext context;
    context.db = fixture.db;
    context.viaProxy = true;
    context.fetchPage = makeHttpFetcher(proxy);

    const QString channel = Tg::channelName(fixture.source.urlTemplate);
    const Models::FetchResult first = Tg::fetchTelegramSource(
        context, fixture.source, channel, QStringLiteral("task-real"), std::chrono::seconds(120));
    GTEST_LOG_(INFO)
        << render(Text::line789(),
                  {{"{}", first.success ? QStringLiteral("true") : QStringLiteral("false")},
                   {"{}", QString::number(first.nodeCount)},
                   {"{}", first.message}})
               .toStdString();
    ASSERT_TRUE(first.success) << first.message.toStdString();
    ASSERT_GT(first.nodeCount, 0);

    const Models::Source reloaded = fixture.db->getSource(fixture.source.id).valueOr(fixture.source);
    const Models::FetchResult second = Tg::fetchTelegramSource(
        context, reloaded, channel, QStringLiteral("task-real"), std::chrono::seconds(120));
    EXPECT_GE(second.nodeCount, first.nodeCount)
        << render(Text::line802(), {{"{}", QString::number(first.nodeCount)},
                                    {"{}", QString::number(second.nodeCount)}})
                   .toStdString();

    const Result<Models::Page<Models::NodeSummary>> listed = fixture.db->listNodes(
        std::nullopt, fixture.source.id, std::nullopt, std::nullopt, std::nullopt, 1, 500,
        std::nullopt);
    ASSERT_TRUE(listed.isOk()) << listed.error().toStdString();
    EXPECT_GT(listed.value().total, 0);
}

// ---- 离线补测：把上面三个网络探针覆盖的分支在不联网的前提下测出来 ----

TEST(Telegram, fetch_channel_accumulates_and_never_deletes_old_nodes)
{
    Result<Fixture> opened =
        makeFixture(QStringLiteral("https://t.me/s/demo"), QStringLiteral("TG demo"));
    ASSERT_TRUE(opened.isOk()) << opened.error().toStdString();
    Fixture fixture = opened.takeValue();

    const QString page = Text::fixtureHtml();
    FakeTransport transport;
    transport.bodies.insert(QString::fromLatin1(kFirstPage), page);
    transport.notModified.append(QString::fromLatin1(kSubUrl));

    Tg::TelegramContext context;
    context.db = fixture.db;
    context.viaProxy = true;
    context.fetchPage = transport.fetcher();
    context.phase = transport.phase();
    context.nodes = transport.nodes();

    const Models::FetchResult first = Tg::fetchTelegramSource(
        context, fixture.source, QStringLiteral("demo"), QStringLiteral("task-1"),
        std::chrono::seconds(30));
    ASSERT_TRUE(first.success) << first.message.toStdString();
    EXPECT_EQ(first.nodeCount, 4);
    EXPECT_EQ(transport.reportedNodes, 4);
    EXPECT_EQ(first.resolvedUrl.toStdString(), QString::fromLatin1(kFirstPage).toStdString());
    // 第一轮 last_fetch_at 是空的 → 按 SEED_PAGES 翻 3 页；假出口只认第一页，翻页失败就收手，
    // 整轮不该被判死。
    ASSERT_EQ(transport.requested.size(), 3);

    // 第二轮：同一频道，正文里少了 vless 与 trojan 两条。只增不删 → 分片总数不该变小。
    // （Qt 6.10 的 QString::replace 返回 QString&，拿返回值数替换次数行不通，改判前后内容。）
    const QString droppedVless =
        QStringLiteral("vless://uuid-uuid-uuid@45.76.12.3:443?security=tls#DE-9<br/>\n");
    const QString droppedTrojan =
        QStringLiteral("trojan://password7@example.org:443?allowInsecure=1#SG");
    ASSERT_TRUE(page.contains(droppedVless));
    ASSERT_TRUE(page.contains(droppedTrojan));
    QString shrunk = page;
    shrunk.replace(droppedVless, QString());
    shrunk.replace(droppedTrojan, QString());
    EXPECT_FALSE(shrunk.contains(droppedVless));
    EXPECT_FALSE(shrunk.contains(droppedTrojan));
    ASSERT_TRUE(shrunk.contains(QLatin1String("ss://")));
    transport.bodies.insert(QString::fromLatin1(kFirstPage), shrunk);

    const Models::Source reloaded = fixture.db->getSource(fixture.source.id).valueOr(fixture.source);
    EXPECT_FALSE(reloaded.lastFetchAt.isNull());
    const Models::FetchResult second = Tg::fetchTelegramSource(
        context, reloaded, QStringLiteral("demo"), QStringLiteral("task-2"),
        std::chrono::seconds(30));
    ASSERT_TRUE(second.success) << second.message.toStdString();
    EXPECT_EQ(second.nodeCount, 4) << second.message.toStdString();

    const Result<QVector<Models::Node>> stored = fixture.db->loadNodes(fixture.source.id);
    ASSERT_TRUE(stored.isOk()) << stored.error().toStdString();
    EXPECT_EQ(stored.value().size(), 4);

    const Models::Source afterFetch = fixture.db->getSource(fixture.source.id).valueOr(fixture.source);
    EXPECT_EQ(afterFetch.lastStatus.toStdString(), std::string("ok"));
    EXPECT_EQ(afterFetch.lastNodeCount, 4);
    // 第二轮只看最新一页（FOLLOW_PAGES）：base + 订阅链接 = 2 次，累计 5 次。
    EXPECT_EQ(transport.requested.size(), 5);
}

TEST(Telegram, fetch_channel_seeds_three_pages_with_before_cursor)
{
    Result<Fixture> opened =
        makeFixture(QStringLiteral("https://t.me/s/demo"), QStringLiteral("TG demo"));
    ASSERT_TRUE(opened.isOk()) << opened.error().toStdString();
    Fixture fixture = opened.takeValue();

    FakeTransport transport;
    const QString page = Text::fixtureHtml();
    transport.bodies.insert(QString::fromLatin1(kFirstPage), page);
    transport.bodies.insert(QString::fromLatin1(kSecondPage), page);

    Tg::TelegramContext context;
    context.db = fixture.db;
    context.viaProxy = true;
    context.fetchPage = transport.fetcher();

    const Models::FetchResult result = Tg::fetchTelegramSource(
        context, fixture.source, QStringLiteral("demo"), QStringLiteral("task-1"),
        std::chrono::seconds(30));
    ASSERT_TRUE(result.success) << result.message.toStdString();
    // 翻页拿最老的消息号当游标；两页都是 198/200，所以游标一直是 198，翻满 SEED_PAGES=3 页。
    ASSERT_EQ(transport.requested.size(), 4);
    EXPECT_EQ(transport.requested.at(0).toStdString(), QString::fromLatin1(kFirstPage).toStdString());
    EXPECT_EQ(transport.requested.at(1).toStdString(), QString::fromLatin1(kSecondPage).toStdString());
    EXPECT_EQ(transport.requested.at(2).toStdString(), QString::fromLatin1(kSecondPage).toStdString());
    // 六条正文里的链接去重后仍是 4 个裸链接，节点数不随页数翻倍。
    EXPECT_EQ(result.nodeCount, 4);
}

TEST(Telegram, fetch_channel_reports_not_modified_on_304)
{
    Result<Fixture> opened =
        makeFixture(QStringLiteral("https://t.me/s/demo"), QStringLiteral("TG demo"));
    ASSERT_TRUE(opened.isOk()) << opened.error().toStdString();
    Fixture fixture = opened.takeValue();
    fixture.db->markSourceFetched(fixture.source.id, QStringLiteral("ok"), 7);

    FakeTransport transport;
    transport.notModified.append(QString::fromLatin1(kFirstPage));
    Tg::TelegramContext context;
    context.db = fixture.db;
    context.viaProxy = true;
    context.fetchPage = transport.fetcher();

    const Models::Source reloaded = fixture.db->getSource(fixture.source.id).valueOr(fixture.source);
    const Models::FetchResult result = Tg::fetchTelegramSource(
        context, reloaded, QStringLiteral("demo"), QStringLiteral("task-1"),
        std::chrono::seconds(30));
    EXPECT_TRUE(result.success);
    // node_count 沿用上一轮的 last_node_count（Rust 的 .max(0) as usize）。
    EXPECT_EQ(result.nodeCount, int(std::max<qint64>(reloaded.lastNodeCount, 0)));
    EXPECT_EQ(result.nodeCount, 7);
    EXPECT_TRUE(result.message.contains(QLatin1String("not modified")));
    EXPECT_EQ(result.resolvedUrl.toStdString(), QString::fromLatin1(kFirstPage).toStdString());
    const Models::Source afterFetch = fixture.db->getSource(fixture.source.id).valueOr(fixture.source);
    EXPECT_EQ(afterFetch.lastStatus.toStdString(), std::string("not modified"));
}

TEST(Telegram, fetch_channel_skips_parse_when_content_unchanged)
{
    Result<Fixture> opened =
        makeFixture(QStringLiteral("https://t.me/s/demo"), QStringLiteral("TG demo"));
    ASSERT_TRUE(opened.isOk()) << opened.error().toStdString();
    Fixture fixture = opened.takeValue();

    const QString page = Text::fixtureHtml();
    FakeTransport transport;
    transport.bodies.insert(QString::fromLatin1(kFirstPage), page);
    transport.notModified.append(QString::fromLatin1(kSecondPage));
    Tg::TelegramContext context;
    context.db = fixture.db;
    context.viaProxy = true;
    context.fetchPage = transport.fetcher();

    const Models::FetchResult first = Tg::fetchTelegramSource(
        context, fixture.source, QStringLiteral("demo"), QStringLiteral("task-1"),
        std::chrono::seconds(30));
    ASSERT_TRUE(first.success) << first.message.toStdString();

    const Models::Source reloaded = fixture.db->getSource(fixture.source.id).valueOr(fixture.source);
    // 落库的 sha256 就是这一页正文的 sha256，last_resolved_url 是 t.me/s/<频道>（不带 ?before）。
    ASSERT_EQ(reloaded.contentSha256.toStdString(), Crypto::sha256Hex(page.toUtf8()).toStdString());
    ASSERT_EQ(reloaded.lastResolvedUrl.toStdString(), QString::fromLatin1(kFirstPage).toStdString());

    transport.requested.clear();
    const Models::FetchResult second = Tg::fetchTelegramSource(
        context, reloaded, QStringLiteral("demo"), QStringLiteral("task-2"),
        std::chrono::seconds(30));
    EXPECT_TRUE(second.success);
    EXPECT_TRUE(second.message.contains(QLatin1String("content unchanged")));
    EXPECT_EQ(second.nodeCount, int(std::max<qint64>(reloaded.lastNodeCount, 0)));
    // 内容没变 → 不翻页、不跟订阅链接，只请求了第一页。
    EXPECT_EQ(transport.requested.size(), 1);
    const Models::Source afterFetch = fixture.db->getSource(fixture.source.id).valueOr(fixture.source);
    EXPECT_EQ(afterFetch.lastStatus.toStdString(), std::string("content unchanged"));
}

TEST(Telegram, fetch_channel_without_any_egress_fails_before_touching_the_network)
{
    Result<Fixture> opened =
        makeFixture(QStringLiteral("https://t.me/s/demo"), QStringLiteral("TG demo"));
    ASSERT_TRUE(opened.isOk()) << opened.error().toStdString();
    Fixture fixture = opened.takeValue();

    FakeTransport transport;
    transport.bodies.insert(QString::fromLatin1(kFirstPage), Text::fixtureHtml());
    Tg::TelegramContext context;
    context.db = fixture.db;
    context.viaProxy = false;
    context.fetchPage = transport.fetcher();
    context.systemProxyUrl = [] { return QString(); }; // 这台机器没开系统代理

    const Models::FetchResult result = Tg::fetchTelegramSource(
        context, fixture.source, QStringLiteral("demo"), QStringLiteral("task-1"),
        std::chrono::seconds(30));
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.nodeCount, 0);
    EXPECT_EQ(result.resolvedUrl.toStdString(), QString::fromLatin1(kFirstPage).toStdString());
    // telegram.rs:303-305 的消息：ASCII 骨架 "Telegram "，73 个码点（见生成器输出）。
    EXPECT_TRUE(result.message.startsWith(QLatin1String("Telegram ")));
    EXPECT_EQ(result.message.size(), 73);
    // 连页面都不该去要：开口之前就把原因说清楚。
    EXPECT_EQ(transport.requested.size(), 0);
    const Models::Source afterFetch = fixture.db->getSource(fixture.source.id).valueOr(fixture.source);
    EXPECT_EQ(afterFetch.lastStatus.toStdString(), result.message.toStdString());
}

TEST(Telegram, fetch_channel_without_working_proxy_reports_system_proxy_reason)
{
    Result<Fixture> opened =
        makeFixture(QStringLiteral("https://t.me/s/demo"), QStringLiteral("TG demo"));
    ASSERT_TRUE(opened.isOk()) << opened.error().toStdString();
    Fixture fixture = opened.takeValue();

    FakeTransport transport;
    transport.bodies.insert(QString::fromLatin1(kFirstPage), Text::fixtureHtml());
    Tg::TelegramContext context;
    context.db = fixture.db;
    context.viaProxy = false;
    context.fetchPage = transport.fetcher();
    context.systemProxyUrl = [] { return QStringLiteral("socks5://127.0.0.1:1"); };
    context.applyProxy = [](const QString &) -> Result<Tg::PageFetcher> {
        return Result<Tg::PageFetcher>::withError(QStringLiteral("proxy rejected"));
    };

    const Models::FetchResult result = Tg::fetchTelegramSource(
        context, fixture.source, QStringLiteral("demo"), QStringLiteral("task-1"),
        std::chrono::seconds(30));
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.nodeCount, 0);
    // telegram.rs:294 的消息是 12 个码点、无 ASCII 骨架。
    EXPECT_EQ(result.message.size(), 12);
    EXPECT_EQ(transport.requested.size(), 0);
}

TEST(Telegram, fetch_channel_records_subscription_url_failures_in_message)
{
    Result<Fixture> opened =
        makeFixture(QStringLiteral("https://t.me/s/demo"), QStringLiteral("TG demo"));
    ASSERT_TRUE(opened.isOk()) << opened.error().toStdString();
    Fixture fixture = opened.takeValue();

    FakeTransport transport;
    transport.bodies.insert(QString::fromLatin1(kFirstPage), Text::fixtureHtml());
    // 翻页回 304：拿不到旧页不该影响整轮，订阅链接则明确失败。
    transport.notModified.append(QString::fromLatin1(kSecondPage));
    Tg::TelegramContext context;
    context.db = fixture.db;
    context.viaProxy = true;
    context.fetchPage = transport.fetcher();
    context.phase = transport.phase();
    context.nodes = transport.nodes();

    const Models::FetchResult result = Tg::fetchTelegramSource(
        context, fixture.source, QStringLiteral("demo"), QStringLiteral("task-1"),
        std::chrono::seconds(30));
    ASSERT_TRUE(result.success) << result.message.toStdString();
    GTEST_LOG_(INFO) << result.message.toStdString();
    ASSERT_EQ(transport.requested.size(), 3);
    EXPECT_EQ(transport.requested.at(2).toStdString(), QString::fromLatin1(kSubUrl).toStdString());
    // 跟进订阅链接只是加分项：失败原因（host: 消息）要留在消息里，但不判死整轮。
    EXPECT_TRUE(result.message.contains(QLatin1String("nodebuf.com:")));
    EXPECT_TRUE(result.message.contains(transport.errorMessage));
    // 阶段汇报顺序：Downloading → Parsing → Writing，节点数汇报一次。
    ASSERT_EQ(transport.phases.size(), 3);
    EXPECT_EQ(transport.phases.at(0).toStdString(),
              std::to_string(int(Tg::TaskPhase::Downloading)));
    EXPECT_EQ(transport.phases.at(1).toStdString(),
              std::to_string(int(Tg::TaskPhase::Parsing)));
    EXPECT_EQ(transport.phases.at(2).toStdString(),
              std::to_string(int(Tg::TaskPhase::Writing)));
    EXPECT_EQ(transport.nodeReports, 1);
    EXPECT_EQ(result.nodeCount, 4);
}

} // namespace
