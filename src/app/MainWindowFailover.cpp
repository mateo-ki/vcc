// 故障自动切换那台发动机（Rust `gui/mod.rs:3327-3626`）。单独一个文件是因为它同时碰
// 设置、调度器、探针、切换记录和外壳状态栏，塞进 MainWindow.cpp 的窗口骨架里会淹掉两边。
#include "app/MainWindow.h"

#include "app/AppContext.h"
#include "core/Log.h"
#include "db/Database.h"
#include "db/DatabaseTelemetry.h"
#include "gui/ProxyPage.h"
#include "gui/UiText.h"
#include "gui/Widgets.h"
#include "platform/ProcessControl.h"
#include "models/Settings.h"
#include "proxy/ProxyManager.h"
#include "proxy/TestUrls.h"
#include "proxy_probe/Reachability.h"

#include <QSystemTrayIcon>
#include <QTimer>

#include <cstdlib>

#include <algorithm>

namespace Gui {

void MainWindow::switchProxyNode(const QString &nodeId, const QString &nodeName, const QString &reason)
{
    // Rust mod.rs:2022-2038：只有"真的换了人"才记一条出口变更（同一颗连点两次不算两次
    // 切换），而这条记录就是「查看切换记录」那个窗口的内容 —— 手动切和自动切都留得下痕迹。
    if (selectedNodeId_ != nodeId) {
        auto db = context_.databasePtr();
        DbTelemetry::SwitchRecord record;
        record.fromNode = selectedNodeId_;
        record.fromName = selectedNodeName_;
        record.toNode = nodeId;
        record.toName = nodeName;
        record.reason = reason;
        context_.runAsync([db, record]() { DbTelemetry::recordEgressSwitch(*db, record); },
                          []() {});
    }

    if (requestTunElevationHandoff(nodeId)) {
        // TUN 模式没提权：交给提权的自己，这一次不在这儿起代理（Rust mod.rs:2059）。
        return;
    }

    selectedNodeId_ = nodeId;
    selectedNodeName_ = nodeName;

    const auto nodeRes = context_.database().getNode(nodeId);
    if (nodeRes.isOk()) {
        selectedNodeServer_ = nodeRes.value().server;
    }

    if (proxyPage_ != nullptr) {
        proxyPage_->setSelectedNode(nodeId, nodeName);
    }
    failoverRouter_.setActive(nodeId);

    const auto status = context_.proxy().status();
    const bool wasRunning = status.localProxyActive();
    if (!nodeRes.isOk()) {
        statusLabel_->setText(QStringLiteral("无法读取选中的节点信息"));
        updateStatusControls();
        return;
    }

    const Models::Node node = nodeRes.value();
    statusLabel_->setText(wasRunning ? Gui::UiText::switchingToNode().arg(nodeName)
                                     : Gui::UiText::startingProxyWithNode().arg(nodeName));
    updateStatusControls();
    // Rust 的 switch_proxy_node 会把 connect/switch_node 的 Result 带回 UI，失败就当场把
    // 这颗节点标掉 —— 不然测速表里它还是"可用"，下一次又会被选中。
    auto outcome = std::make_shared<Status>();
    context_.runAsync(
        [this, node, wasRunning, outcome]() {
            *outcome = wasRunning ? context_.proxy().switchNode(node) : context_.proxy().start(node);
        },
        [this, node, nodeName, wasRunning, outcome]() {
            if (outcome->isErr()) {
                markNodeUnavailable(node.id);
                statusLabel_->setText(Gui::UiText::switchNodeFailed().arg(outcome->error()));
            } else if (wasRunning) {
                statusLabel_->setText(Gui::UiText::switchedSmoothly().arg(nodeName));
            } else {
                statusLabel_->setText(Gui::UiText::proxyStartedWithNode().arg(nodeName));
            }
            updateStatusControls();
            if (proxyPage_ != nullptr) {
                proxyPage_->requestFullReload();
                proxyPage_->refresh();
            }
        }, this);
}

void MainWindow::syncRouterConfig()
{
    // 调度器参数跟随系统设置，避免两处真相（Rust mod.rs:3627-3636）。
    Router::RouterConfig cfg = failoverRouter_.cfg;
    cfg.latencyThresholdMs = quint64(qMax<qint64>(context_.settings().failoverLatencyMs, 200));
    cfg.bandwidthThresholdKbps = context_.settings().bandwidthGuard
        ? double(qMax<qint64>(context_.settings().bandwidthMinKbps, 1))
        : 0.0;
    failoverRouter_.cfg = cfg;
}

void MainWindow::seedRouterCandidates()
{
    // 用最近一次测速结果给调度器播种，否则刚启动时它谁都不认识（Rust mod.rs:3639-3653）。
    const QString keywords = context_.settings().excludeKeywords;
    const Router::Instant now = Router::nowMonotonic();
    QVector<QPair<QString, qint64>> rows;
    if (proxyPage_ != nullptr) {
        for (const Router::BackupCandidate &node : proxyPage_->backupCandidates()) {
            if (node.lastLatencyMs.has_value() && !Models::nameExcluded(node.name, keywords)) {
                rows.push_back(qMakePair(node.id, qint64(*node.lastLatencyMs)));
            }
        }
    }
    std::sort(rows.begin(), rows.end(),
              [](const QPair<QString, qint64> &left, const QPair<QString, qint64> &right) {
                  return left.second < right.second;
              });
    for (int index = 0; index < rows.size() && index < 60; ++index) {
        failoverRouter_.seed(rows.at(index).first,
                             quint64(qMax<qint64>(0, rows.at(index).second)), true, now);
    }
}

void MainWindow::checkFailover()
{
    // 探测本身**不受 auto_failover 开关影响**：表头那句"已连通"要说实话，就必须一直有实测
    // 在跑。开关只决定"测出问题之后要不要自动换出口"（Rust mod.rs:3327-3341）。
    const Proxy::ProxyRuntimeStatus status = context_.proxy().status();
    if (status.state != Proxy::ProxyState::Running && status.state != Proxy::ProxyState::Degraded) {
        return;
    }
    if (selectedNodeId_.isEmpty()) {
        return;
    }
    const Router::Instant now = Router::nowMonotonic();
    if (failoverProbeAt_.has_value() && Router::durationSecs(now - *failoverProbeAt_) < 60) {
        return;
    }
    // 先占位，避免结果回来前重复探测。
    failoverProbeAt_ = now;
    syncRouterConfig();
    seedRouterCandidates();
    failoverRouter_.setActive(selectedNodeId_);

    const QString nodeId = selectedNodeId_;
    const QString nodeName = selectedNodeName_.isEmpty() ? nodeId : selectedNodeName_;
    // Rust：(failover_latency_ms * 2) 夹在 3000..8000。
    const int timeoutMs = int(qBound(3000LL, qint64(context_.settings().failoverLatencyMs) * 2,
                                     8000LL));
    Proxy::ProxyManager *manager = &context_.proxy();
    // 基准站点来自设置（每行一个）；一条都没填就用内置的 Google 204（Rust 只有这一条）。
    const QStringList baselines =
        ProxyProbe::probeBaselineUrls(context_.settings().failoverProbeUrls);
    auto box = std::make_shared<Proxy::HealthProbeResult>();
    context_.runAsync(
        [box, manager, timeoutMs, baselines]() { *box = manager->probeHealth(timeoutMs, baselines); },
        [this, nodeId, nodeName, box]() {
            handleFailoverProbe(nodeId, nodeName, box->ok, qint64(box->latencyMs), box->error);
        },
        this);
}

void MainWindow::handleFailoverProbe(const QString &nodeId, const QString &nodeName, bool ok,
                                     qint64 latencyMs, const QString &error)
{
    // 探测期间用户可能已经手动换过节点：旧结果直接丢（Rust mod.rs:3380-3383）。
    if (selectedNodeId_ != nodeId) {
        return;
    }
    // 每一次实测都落库并刷回界面 —— 状态卡那句"实测"从此只信这条，不信 ProxyState。
    auto db = context_.databasePtr();
    context_.runAsync(
        [db, ok, latencyMs, error, nodeId, nodeName]() {
            DbTelemetry::recordEgressHealth(
                *db, ok, ok && latencyMs > 0 ? std::optional<qint64>(latencyMs) : std::nullopt,
                // 失败原因用探测自己带回来的那条（含状态码/传输错误），不再固定写"没取到 204"
                // —— 基准站点现在可以是用户自己填的。
                ok ? QString() : (error.isEmpty() ? QStringLiteral("实测未通过") : error),
                nodeId, nodeName,
                QStringLiteral("periodic"));
        },
        [this]() {
            if (proxyPage_ != nullptr) {
                proxyPage_->noteEgressHealthUpdated();
            }
        },
        this);

    if (!context_.settings().autoFailover) {
        // 只测不换：开关关掉时健康数据照记，出口绝不自动被换走。
        return;
    }
    syncRouterConfig();
    Router::Sample sample;
    sample.ok = ok;
    sample.latencyMs = latencyMs > 0 ? std::optional<quint64>(quint64(latencyMs)) : std::nullopt;
    applyRouterDecision(nodeId, nodeName, failoverRouter_.observe(nodeId, sample));
}

void MainWindow::applyRouterDecision(const QString &nodeId, const QString &nodeName,
                                     const Router::Decision &decision)
{
    // Rust 在这里还有一支"url-test 开着时把换节点交回内核"的分支；界面上那个开关
    // 2026-10-06 已撤、`Proxy::configForUi()` 把 url_test 一律归成 false，
    // 所以那条分支在两边都不可达，这里不写第二份。
    switch (decision.kind) {
    case Router::DecisionKind::Stay:
        return;
    case Router::DecisionKind::BreakerOpened:
        failoverLastReason_ = QStringLiteral("%1 %2，正在寻找替补节点").arg(nodeName, decision.reason);
        statusLabel_->setText(failoverLastReason_);
        beginFailoverSwitch(nodeId, QString(), Gui::UiText::failoverBreakerBlacklisted());
        return;
    case Router::DecisionKind::SwitchTo:
        // 调度器给的目标也要先过一遍 TCP 复检，复检过了才真切。
        beginFailoverSwitch(nodeId, decision.nodeId, decision.reason);
        return;
    }
}

void MainWindow::beginFailoverSwitch(const QString &currentId, const QString &preferredId,
                                     const QString &reason)
{
    if (failoverSearching_) {
        failoverLastReason_ = Gui::UiText::failoverSearching();
        statusLabel_->setText(failoverLastReason_);
        return;
    }
    const QStringList blocked = failoverRouter_.blacklisted();
    const QSet<QString> blacklisted(blocked.begin(), blocked.end());
    QVector<Router::BackupCandidate> shortlist;
    if (proxyPage_ != nullptr) {
        shortlist = Router::pickBackups(proxyPage_->backupCandidates(), currentId, blacklisted,
                                        context_.settings().excludeKeywords, 8);
    }
    if (!preferredId.isEmpty()) {
        for (int index = 0; index < shortlist.size(); ++index) {
            if (shortlist.at(index).id == preferredId) {
                shortlist.prepend(shortlist.takeAt(index));
                break;
            }
        }
    }

    failoverSearching_ = true;
    failoverFrom_ = qMakePair(currentId, selectedNodeName_);
    statusLabel_->setText(shortlist.isEmpty()
                              ? Gui::UiText::failoverNoSecondCandidate().arg(reason)
                              : Gui::UiText::failoverTcpRecheck().arg(reason).arg(shortlist.size()));

    // 复检要连一堆端口，每个最多 2.5 秒 —— 这一步绝不能压在 GUI 线程上。
    auto pool = std::make_shared<QVector<Router::BackupCandidate>>(shortlist);
    auto chosen = std::make_shared<Router::BackupCandidate>();
    auto failure = std::make_shared<QString>();
    auto db = context_.databasePtr();
    context_.runAsync(
        [pool, chosen, failure, db, currentId]() {
            if (pool->isEmpty()) {
                // 控制台这批里没有第二个可用节点：改从库里最新测通的现查（Rust :3556-3573）。
                const auto top = db->topVerifiedNodes(12, currentId);
                if (top.isOk()) {
                    for (const Models::Node &node : top.value()) {
                        Router::BackupCandidate candidate;
                        candidate.id = node.id;
                        candidate.name = node.name;
                        candidate.host = node.server;
                        candidate.port = quint16(node.port);
                        pool->push_back(candidate);
                    }
                }
            }
            if (pool->isEmpty()) {
                *failure = Gui::UiText::failoverNoneVerified();
                return;
            }
            QVector<ProxyProbe::ReachTarget> targets;
            targets.reserve(pool->size());
            for (const Router::BackupCandidate &candidate : *pool) {
                ProxyProbe::ReachTarget target;
                target.id = candidate.id;
                target.name = candidate.name;
                target.host = candidate.host;
                target.port = candidate.port;
                targets.push_back(target);
            }
            const auto reachable = ProxyProbe::firstReachable(targets, 2500);
            if (!reachable.has_value()) {
                *failure = Gui::UiText::failoverAllUnreachable().arg(pool->size());
                return;
            }
            for (const Router::BackupCandidate &candidate : *pool) {
                if (candidate.id == reachable->first) {
                    *chosen = candidate;
                    return;
                }
            }
            chosen->id = reachable->first;
            chosen->name = reachable->second;
        },
        [this, chosen, failure]() { onFailoverTarget(*chosen, *failure); },
        this);
}

void MainWindow::onFailoverTarget(const Router::BackupCandidate &chosen, const QString &failure)
{
    failoverSearching_ = false;
    const auto from = failoverFrom_;
    failoverFrom_.reset();

    if (!failure.isEmpty() || chosen.id.isEmpty()) {
        const QString reason = failure.isEmpty() ? Gui::UiText::failoverNoneVerified() : failure;
        failoverLastReason_ = Gui::UiText::failoverButReason().arg(reason);
        statusLabel_->setText(failoverLastReason_);
        if (from.has_value()) {
            recordFailoverMiss(from->first, from->second, reason);
        }
        if (proxyPage_ != nullptr) {
            proxyPage_->setFailoverStatus(failoverSwitches_, failoverLastReason_);
        }
        return;
    }

    ++failoverSwitches_;
    failoverLastReason_ = Gui::UiText::failoverSwitchedTo().arg(chosen.name).arg(failoverSwitches_);
    statusLabel_->setText(failoverLastReason_);
    if (proxyPage_ != nullptr) {
        proxyPage_->setFailoverStatus(failoverSwitches_, failoverLastReason_);
    }
    switchProxyNode(chosen.id, chosen.name, Gui::UiText::switchReasonAuto());
}

void MainWindow::recordFailoverMiss(const QString &fromId, const QString &fromName,
                                    const QString &reason)
{
    // "该切但没切成"也进切换历史。以前这种只写在一行状态栏里，滑过去就查无实据，
    // 于是"我开了自动切换为什么没换"只能靠人翻库猜（Rust mod.rs:3611-3625）。
    auto db = context_.databasePtr();
    DbTelemetry::SwitchRecord record;
    record.fromNode = fromId;
    record.fromName = fromName;
    // 空串而不是 NULL：DAO 用 (to_node, reason) 去重连续相同记录，NULL 参与拼接会永远
    // 不相等，抖动时每两分钟刷一条一模一样的。
    record.toNode = QString();
    record.reason = Gui::UiText::failoverMissPrefix().arg(reason);
    context_.runAsync([db, record]() { DbTelemetry::recordEgressSwitch(*db, record); }, []() {});
}


bool MainWindow::requestTunElevationHandoff(const QString &nodeId)
{
    const Proxy::MihomoConfigOptions config = Proxy::configForUi(context_.proxy().config());
    if (!Gui::requiresTunElevation(config.mode, Platform::isElevated())) {
        return false;
    }
    const Result<QString> encoded = Proxy::MihomoConfigOptions::encodeHandoffConfig(config);
    if (encoded.isErr()) {
        statusLabel_->setText(Gui::UiText::elevatedConfigSerializeFailed().arg(encoded.error()));
        return true;
    }
    const QStringList args{Platform::elevatedProxyConfigFlag(), encoded.value(),
                           QStringLiteral("--start-proxy-node"), nodeId};
    // requestElevation 是同步的：ShellExecuteW("runas") 要等 UAC 那张对话框有结果才返回，
    // 这期间 GUI 线程一帧都不跑，窗口就停在"白屏 + 未响应"上（2026-10-12 他报的白窗口，
    // 一半是这个）。所以先把主窗口收起来再弹 UAC；被拒就原样显示回来。
    const bool wasVisible = isVisible();
    hide();
    QCoreApplication::processEvents();
    const Status requested = Platform::requestElevation(args);
    if (requested.isErr()) {
        if (wasVisible) {
            show();
        }
        // 用户在 UAC 上点了"否"，或系统禁用了提权：这一次启动就停在这里，
        // 不许悄悄退回系统代理模式（那等于替他改了工作模式）。
        statusLabel_->setText(Gui::UiText::elevatedRequestFailed().arg(requested.error()));
        return true;
    }
    statusLabel_->setText(Gui::UiText::elevatedRequested());
    closeAfterElevation_ = true;
    // Rust 那边是 startup_log + tray.shutdown() + 100ms + process::exit(0)：必须绕开
    // 正常退出路径，否则 aboutToQuit 会把子进程刚接手的代理停掉、把系统代理还原成关。
    Log::startup(QStringLiteral("close_after_elevation -> exiting parent so the elevated instance takes the lock"));
    if (trayIcon_ != nullptr) {
        trayIcon_->hide();
    }
    QTimer::singleShot(150, this, [this]() {
        QCoreApplication::processEvents();
        std::_Exit(0);
    });
    return true;
}

void MainWindow::startHandoffNode(const QString &nodeId)
{
    const auto nodeRes = context_.database().getNode(nodeId);
    if (!nodeRes.isOk()) {
        statusLabel_->setText(QStringLiteral("无法读取选中的节点信息"));
        return;
    }
    const Models::Node node = nodeRes.value();
    selectedNodeId_ = node.id;
    selectedNodeName_ = node.name;
    selectedNodeServer_ = node.server;
    if (proxyPage_ != nullptr) {
        proxyPage_->setSelectedNode(node.id, node.name);
    }
    failoverRouter_.setActive(node.id);
    statusLabel_->setText(Gui::UiText::startingProxyWithNode().arg(node.name));
    updateStatusControls();
    auto outcome = std::make_shared<Status>();
    context_.runAsync(
        [this, node, outcome]() { *outcome = context_.proxy().start(node); },
        [this, node, outcome]() {
            if (outcome->isErr()) {
                markNodeUnavailable(node.id);
                statusLabel_->setText(Gui::UiText::switchNodeFailed().arg(outcome->error()));
            } else {
                statusLabel_->setText(Gui::UiText::proxyStartedWithNode().arg(node.name));
            }
            updateStatusControls();
            if (proxyPage_ != nullptr) {
                proxyPage_->requestFullReload();
                proxyPage_->refresh();
            }
        },
        this);
}

} // namespace Gui
