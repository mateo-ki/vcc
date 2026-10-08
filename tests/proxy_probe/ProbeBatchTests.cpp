#include <gtest/gtest.h>

#include <QSemaphore>
#include <QString>
#include <QVector>

#include <optional>
#include <stdexcept>
#include <utility>

#include "core/JsonUtil.h"
#include "export/ClashProxy.h"
#include "proxy/KernelConfigMessages.h"
#include "proxy/SingBoxConfig.h"
#include "proxy/XrayConfig.h"
#include "proxy_probe/MihomoProbe.h"
#include "proxy_probe/ProbeKernel.h"
#include "proxy_probe/ProbeMessages.h"
#include "proxy_probe/ProbeTypes.h"
#include "proxy_probe/SingBoxProbe.h"
#include "proxy_probe/XrayProbe.h"

using namespace ProxyProbe;

namespace {

/// 期望文本写成 UTF-8 十六进制，来源是 src/proxy_probe/dump-test-hex.mjs（直接从 Rust
/// 字面量推导）。手打中文在这个环境里会被静默丢字符，所以这里一个字都不写。
QString fromHex(const char *hex)
{
    return QString::fromUtf8(QByteArray::fromHex(QByteArray(hex)));
}

/// 暂不支持协议
QString notSupportedProtocol()
{
    return fromHex("E69A82E4B88DE694AFE68C81E58D8FE8AEAE");
}

/// 测速批次失败
QString batchFailed()
{
    return fromHex("E6B58BE9809FE689B9E6ACA1E5A4B1E8B4A5");
}

/// 校验任务失败
QString validateTaskFailed()
{
    return fromHex("E6A0A1E9AA8CE4BBBBE58AA1E5A4B1E8B4A5");
}

Models::Node makeNode(const QString &id, const QString &protocol, const Json &config)
{
    Models::Node node;
    node.id = id;
    node.protocol = protocol;
    node.name = QStringLiteral("test");
    node.server = JsonUtil::textOrEmpty(config, "server");
    node.port = int(JsonUtil::intOr(config, "port", 0));
    node.config = config;
    node.createdAt = TimeUtil::OptionalUtc::now();
    node.updatedAt = node.createdAt;
    return node;
}

Models::Node makeSupportedNode(const QString &id)
{
    Json config;
    config["server"] = "1.2.3.4";
    config["port"] = 443;
    config["uuid"] = "11111111-1111-1111-1111-111111111111";
    config["network"] = "ws";
    config["security"] = "tls";
    return makeNode(id, QStringLiteral("vless"), config);
}

PreparedProxy makePrepared(qsizetype index, const QString &id)
{
    PreparedProxy proxy;
    proxy.index = index;
    proxy.id = id;
    proxy.name = QStringLiteral("probe-%1-%2").arg(index).arg(id);
    proxy.config = Json::object();
    proxy.config["name"] = proxy.id.toStdString();
    proxy.config["type"] = "http";
    proxy.config["server"] = "example.com";
    proxy.config["port"] = 80;
    return proxy;
}

/// 只按 id 前缀判失败的校验器：不起 mihomo，就能单测二分隔离的编排。
BatchValidator rejectIdsStartingWith(const QString &prefix, const QString &reason)
{
    return [prefix, reason](const QVector<PreparedProxy> &batch) {
        for (const PreparedProxy &proxy : batch) {
            if (proxy.id.startsWith(prefix)) {
                return Result<std::optional<QString>>::withValue(reason);
            }
        }
        return Result<std::optional<QString>>::withValue(std::optional<QString>());
    };
}

}

TEST(ProbeBatching, ChunksKeepInputOrder)
{
    QVector<Models::Node> nodes;
    for (int index = 0; index < 10; ++index) {
        nodes.append(makeSupportedNode(QStringLiteral("n-%1").arg(index)));
    }
    const QVector<QVector<Models::Node>> batches = chunkNodes(nodes, 3);
    ASSERT_EQ(batches.size(), 4);
    EXPECT_EQ(batches.at(0).size(), 3);
    EXPECT_EQ(batches.at(3).size(), 1);
    EXPECT_EQ(batches.at(3).at(0).id.toStdString(), std::string("n-9"));
    // batch_size.max(1)：0 不能变成死循环，退化成"一个节点一批"。
    const QVector<QVector<Models::Node>> singles = chunkNodes(nodes, 0);
    ASSERT_EQ(singles.size(), 10);
    EXPECT_EQ(singles.at(0).size(), 1);
    EXPECT_EQ(singles.at(9).at(0).id.toStdString(), std::string("n-9"));
    EXPECT_EQ(chunkNodes(nodes, 8192).size(), 1);
    EXPECT_TRUE(chunkNodes(QVector<Models::Node>(), 256).isEmpty());
}

TEST(ProbeBatching, KernelBatchCountsMatchRust)
{
    // Rust 的 div_ceil(64)：0 个节点是 0 批。
    EXPECT_EQ(Proxy::singBoxBatchCount(0), 0);
    EXPECT_EQ(Proxy::singBoxBatchCount(1), 1);
    EXPECT_EQ(Proxy::singBoxBatchCount(64), 1);
    EXPECT_EQ(Proxy::singBoxBatchCount(65), 2);
    EXPECT_EQ(Proxy::xrayBatchCount(129), 3);
    EXPECT_EQ(Proxy::kSingBoxBatchSize, 64);
    EXPECT_EQ(Proxy::kXrayBatchSize, 64);
}

TEST(ProbeBatching, FailedBatchStillReportsEveryNode)
{
    MihomoBatchOutcome outcome;
    outcome.ids = {QStringLiteral("a"), QStringLiteral("b")};
    outcome.results = Result<QVector<ProbeResult>>::withError(QStringLiteral("boom"));
    QVector<ProbeResult> results;
    const QString message = Proxy::Messages::fill(Messages::mihomoBatchFailed(),
                                                  {{"{error}", QStringLiteral("boom")}});
    appendBatchOutcome(&results, outcome, message);
    ASSERT_EQ(results.size(), 2);
    EXPECT_EQ(results.at(0).id.toStdString(), std::string("a"));
    EXPECT_FALSE(results.at(0).ok);
    EXPECT_FALSE(results.at(0).latencyMs.has_value());
    EXPECT_EQ(results.at(0).message, message);
    EXPECT_EQ(results.at(0).message, QStringLiteral("Mihomo ") + batchFailed() + QStringLiteral(": boom"));

    QVector<ProbeResult> appended;
    ProbeResult first;
    first.id = QStringLiteral("x");
    first.ok = true;
    ProbeResult second;
    second.id = QStringLiteral("y");
    QVector<ProbeResult> batchResults{first, second};
    MihomoBatchOutcome good;
    good.ids = {QStringLiteral("x"), QStringLiteral("y")};
    good.results = Result<QVector<ProbeResult>>::withValue(batchResults);
    appendBatchOutcome(&appended, good, message);
    ASSERT_EQ(appended.size(), 2);
    // 成功批次按原顺序追加。
    EXPECT_EQ(appended.at(0).id.toStdString(), std::string("x"));
    EXPECT_TRUE(appended.at(1).ok == false);
}

TEST(ProbePrepare, MarksUnsupportedProtocolsAndKeepsSlots)
{
    QVector<Models::Node> nodes;
    nodes.append(makeSupportedNode(QStringLiteral("vless-id")));
    nodes.append(makeSupportedNode(QStringLiteral("second-id")));
    Json mieru;
    mieru["server"] = "1.2.3.4";
    mieru["port"] = 443;
    mieru["password"] = "secret";
    nodes.append(makeNode(QStringLiteral("mieru-id"), QStringLiteral("mieru"), mieru));

    const PreparedProxies prepared = prepareProxies(nodes);
    ASSERT_EQ(prepared.results.size(), 3);
    // Rust 的 prepares_supported_nodes_and_reports_unsupported_protocols：认识的协议槽位留空。
    EXPECT_FALSE(prepared.results[0].has_value());
    EXPECT_FALSE(prepared.results[1].has_value());
    ASSERT_TRUE(prepared.results[2].has_value());
    EXPECT_EQ(prepared.results[2]->id.toStdString(), std::string("mieru-id"));
    EXPECT_EQ(prepared.results[2]->message,
              QStringLiteral("Mihomo ") + notSupportedProtocol() + QStringLiteral(": mieru"));
    EXPECT_EQ(Export::nodeToClashProxy(nodes.at(2)).has_value(), false);

    // 名字带下标，且写回配置的 name：控制器只按名字认代理。
    ASSERT_EQ(prepared.prepared.size(), 2);
    for (qsizetype index = 0; index < prepared.prepared.size(); ++index) {
        EXPECT_EQ(prepared.prepared.at(index).name.toStdString(),
                  QStringLiteral("probe-%1-%2")
                      .arg(prepared.prepared.at(index).index)
                      .arg(prepared.prepared.at(index).id)
                      .toStdString());
        EXPECT_EQ(prepared.prepared.at(index).config.at("name"),
                  prepared.prepared.at(index).name.toStdString());
        EXPECT_FALSE(prepared.results.at(prepared.prepared.at(index).index).has_value());
    }
}

TEST(ProbePrepare, EmptyInputYieldsNoSlots)
{
    const PreparedProxies prepared = prepareProxies(QVector<Models::Node>());
    EXPECT_TRUE(prepared.prepared.isEmpty());
    EXPECT_TRUE(prepared.results.isEmpty());
}

TEST(ProbeMihomoConfig, BatchDocumentCarriesProxiesAndSelectGroup)
{
    QVector<PreparedProxy> prepared{makePrepared(0, QStringLiteral("alpha")),
                                    makePrepared(1, QStringLiteral("beta"))};
    const Json config = buildBatchProbeConfig(prepared, 9090);
    EXPECT_EQ(config.at("external-controller"), "127.0.0.1:9090");
    // 测速用的控制器不带密钥：Rust 写死 secret: ""。
    EXPECT_EQ(config.at("secret"), "");
    EXPECT_EQ(config.at("allow-lan"), false);
    EXPECT_EQ(config.at("ipv6"), false);
    EXPECT_EQ(config.at("mode"), "rule");
    EXPECT_EQ(config.at("log-level"), "warning");
    ASSERT_TRUE(config.at("proxies").is_array());
    EXPECT_EQ(config.at("proxies").size(), 2u);
    EXPECT_EQ(config.at("proxies").at(0).at("name"), "alpha");
    ASSERT_TRUE(config.at("proxy-groups").is_array());
    EXPECT_EQ(config.at("proxy-groups").size(), 1u);
    EXPECT_EQ(config.at("proxy-groups").at(0).at("name"), "PROXY");
    EXPECT_EQ(config.at("proxy-groups").at(0).at("type"), "select");
    // select 组里引用的是代理名（probe-<index>-<id>），不是节点名。
    EXPECT_EQ(JsonUtil::dump(Json(config.at("proxy-groups").at(0).at("proxies"))),
              QStringLiteral("[\"probe-0-alpha\",\"probe-1-beta\"]"));
    EXPECT_EQ(JsonUtil::dump(Json(config.at("rules"))), QStringLiteral("[\"MATCH,PROXY\"]"));
}

TEST(ProbeIsolation, RejectsOnlyTheInvalidMemberOfAMixedBatch)
{
    // Rust 的 isolates_invalid_proxy_without_rejecting_valid_batch_members，
    // 但把 mihomo -t 换成注入的校验器：单测不许起内核进程。
    QVector<PreparedProxy> prepared{makePrepared(0, QStringLiteral("valid")),
                                    makePrepared(1, QStringLiteral("invalid"))};
    Result<IsolationOutcome> isolated =
        isolateInvalidProxies(prepared, rejectIdsStartingWith(QStringLiteral("invalid"),
                                                              QStringLiteral("bad type")));
    ASSERT_TRUE(isolated.isOk()) << isolated.error().toStdString();
    ASSERT_EQ(isolated.value().valid.size(), 1);
    EXPECT_EQ(isolated.value().valid.at(0).id.toStdString(), std::string("valid"));
    ASSERT_EQ(isolated.value().rejected.size(), 1);
    EXPECT_EQ(isolated.value().rejected.at(0).first.id.toStdString(), std::string("invalid"));
    EXPECT_EQ(isolated.value().rejected.at(0).second.toStdString(), std::string("bad type"));
}

TEST(ProbeIsolation, DescendsToTheSingleBadProxyAndSortsByIndex)
{
    QVector<PreparedProxy> prepared;
    for (qsizetype index = 0; index < 9; ++index) {
        prepared.append(makePrepared(index,
                                     index == 3 ? QStringLiteral("invalid")
                                                : QStringLiteral("ok-%1").arg(index)));
    }
    Result<IsolationOutcome> isolated =
        isolateInvalidProxies(prepared, rejectIdsStartingWith(QStringLiteral("invalid"),
                                                              QStringLiteral("nope")));
    ASSERT_TRUE(isolated.isOk()) << isolated.error().toStdString();
    EXPECT_EQ(isolated.value().valid.size(), 8);
    ASSERT_EQ(isolated.value().rejected.size(), 1);
    EXPECT_EQ(isolated.value().rejected.at(0).first.index, 3);
    // valid 按 index 排好，结果槽位才不会写错位置。
    for (qsizetype index = 1; index < isolated.value().valid.size(); ++index) {
        EXPECT_LT(isolated.value().valid.at(index - 1).index,
                  isolated.value().valid.at(index).index);
    }
}

TEST(ProbeIsolation, EverythingInvalidYieldsNothingValid)
{
    QVector<PreparedProxy> prepared{makePrepared(0, QStringLiteral("invalid-1")),
                                    makePrepared(1, QStringLiteral("invalid-2"))};
    Result<IsolationOutcome> isolated =
        isolateInvalidProxies(prepared, rejectIdsStartingWith(QStringLiteral("invalid"),
                                                              QStringLiteral("bad")));
    ASSERT_TRUE(isolated.isOk()) << isolated.error().toStdString();
    EXPECT_TRUE(isolated.value().valid.isEmpty());
    ASSERT_EQ(isolated.value().rejected.size(), 2);
    EXPECT_EQ(isolated.value().rejected.at(0).first.index, 0);
    EXPECT_EQ(isolated.value().rejected.at(1).first.index, 1);
}

TEST(ProbeIsolation, ValidatorErrorBubblesUpAsBatchFailure)
{
    QVector<PreparedProxy> prepared{makePrepared(0, QStringLiteral("a"))};
    const BatchValidator broken = [](const QVector<PreparedProxy> &) {
        return Result<std::optional<QString>>::withError(QStringLiteral("write config failed"));
    };
    Result<IsolationOutcome> isolated = isolateInvalidProxies(prepared, broken);
    ASSERT_TRUE(isolated.isErr());
    EXPECT_EQ(isolated.error().toStdString(), std::string("write config failed"));
}

TEST(ProbeIsolation, ThrowingValidatorUsesRustJoinFailureText)
{
    QVector<PreparedProxy> prepared{makePrepared(0, QStringLiteral("a"))};
    const BatchValidator throwing = [](const QVector<PreparedProxy> &) -> Result<std::optional<QString>> {
        throw std::runtime_error("boom");
    };
    Result<IsolationOutcome> isolated = isolateInvalidProxies(prepared, throwing);
    ASSERT_TRUE(isolated.isErr());
    EXPECT_EQ(isolated.error(), validateTaskFailed() + QStringLiteral(": boom"));
}

TEST(ProbeEntryPoints, EmptyNodeListNeverLooksForAKernel)
{
    // 三个入口都要在 find_kernel_executable 之前返回，所以空输入是可以在单测里跑的
    // （非空输入会真的去找内核并起进程，测试里绝对不许）。
    QSemaphore networkLimit(kProxyConcurrency);
    for (size_t round = 0; round < 3; ++round) {
        QVector<ProbeResult> results;
        Result<QVector<ProbeResult>> outcome = Result<QVector<ProbeResult>>::withError(
            QStringLiteral("not dispatched"));
        switch (round) {
        case 0:
            outcome = verifyNodesWithMihomo(QVector<Models::Node>(), 1000, ProgressFn(),
                                            networkLimit, kMihomoBatchSize, false);
            break;
        case 1:
            outcome = verifyNodesWithSingBox(QVector<Models::Node>(), 1000, ProgressFn(),
                                             networkLimit, size_t(Proxy::kSingBoxBatchSize), false);
            break;
        default:
            outcome = verifyNodesWithXray(QVector<Models::Node>(), 1000, ProgressFn(),
                                          networkLimit, size_t(Proxy::kXrayBatchSize), false);
            break;
        }
        ASSERT_TRUE(outcome.isOk()) << outcome.error().toStdString();
        EXPECT_TRUE(outcome.value().isEmpty());
    }
    EXPECT_TRUE(verifyNodesWithMihomo(QVector<Models::Node>(), 1000).isOk());
    EXPECT_TRUE(verifyNodesWithSingBox(QVector<Models::Node>(), 1000).isOk());
    EXPECT_TRUE(verifyNodesWithXray(QVector<Models::Node>(), 1000).isOk());
}

TEST(ProbeEntryPoints, KernelDispatchHandlesEmptyInputForEveryKind)
{
    QSemaphore networkLimit(kProxyConcurrency);
    for (const Kernel::Kind kind : {Kernel::Kind::Mihomo, Kernel::Kind::SingBox,
                                    Kernel::Kind::Xray}) {
        const Result<QVector<ProbeResult>> outcome =
            verifyNodesWithKernel(kind, QVector<Models::Node>(), 1500, ProgressFn(), networkLimit,
                                  kPersistentMihomoBatchSize, true);
        ASSERT_TRUE(outcome.isOk()) << outcome.error().toStdString();
        EXPECT_TRUE(outcome.value().isEmpty());
    }
}

TEST(ProbeEntryPoints, BatchProgressAccountsAcrossBatches)
{
    // 不碰内核：直接喂 appendBatchOutcome + emitProgress，复刻 limited 里的进度算法。
    QVector<ProbeResult> results;
    QVector<ProbeProgress> seen;
    const ProgressFn progress = [&seen](const ProbeProgress &state) { seen.append(state); };
    const qsizetype total = 4;
    const qsizetype batchTotal = 2;
    for (qsizetype batchIndex = 0; batchIndex < batchTotal; ++batchIndex) {
        MihomoBatchOutcome outcome;
        ProbeResult passed;
        passed.id = QStringLiteral("n-%1").arg(batchIndex * 2);
        passed.ok = true;
        ProbeResult rejected;
        rejected.id = QStringLiteral("n-%1").arg(batchIndex * 2 + 1);
        rejected.ok = false;
        outcome.results = Result<QVector<ProbeResult>>::withValue(
            QVector<ProbeResult>{passed, rejected});
        appendBatchOutcome(&results, outcome, QString());
        emitProgress(progress, results.size(), countPassed(results), total, 11, batchIndex + 1,
                     batchTotal);
    }
    ASSERT_EQ(seen.size(), 2);
    EXPECT_EQ(seen.at(0).done, size_t(2));
    EXPECT_EQ(seen.at(0).ok, size_t(1));
    EXPECT_EQ(seen.at(0).fail, size_t(1));
    EXPECT_EQ(seen.at(1).done, size_t(4));
    EXPECT_EQ(seen.at(1).ok, size_t(2));
    EXPECT_EQ(*seen.at(1).batchIndex, size_t(2));
    EXPECT_EQ(*seen.at(1).batchTotal, size_t(2));
    EXPECT_EQ(countPassed(results), 2);
}
