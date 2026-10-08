#pragma once

#include <QString>
#include <QVector>

#include <optional>
#include <utility>

namespace ProxyProbe {

/// TCP 层可达性检查的目标。Rust 侧直接吃 router::BackupCandidate，这里拆成扁平结构，
/// 免得"要不要能连上"这种网络判断反过来依赖选路模块。
struct ReachTarget {
    QString id;
    QString name;
    QString host;
    quint16 port = 0;
};

/// 上一次延迟最好就用谁（`candidates` 已按延迟从低到高排好），并发 TCP connect。
///
/// 为什么要这一步：库里那条"测通"可能已经是几小时前的。2026-10-07 实测过 ——
/// 自动切换照 08:24 的 90ms 记录切进一个端口早就不监听的 socks5，结果整个境外
/// 不通，而记录上那个节点仍是"测通"状态。
std::optional<std::pair<QString, QString>> firstReachable(const QVector<ReachTarget> &candidates,
                                                          int timeoutMs);

}
