#pragma once

#include <QString>
#include <QVector>

#include "models/Node.h"

// 对应 Rust 的 src/export_quality.rs：判断哪些节点值得导出/推送。
// 单纯 TCP 延迟对免费订阅是个很差的排序依据（开放端口/诱饵站点反而得分高），
// 所以打分里叠了协议偏好、TLS/REALITY、凭据、传输方式、端口和名称噪声。
namespace ExportQuality {

/// 客户端导出用的打分，越高越好（Rust 的 export_score）。
qint64 exportScore(const Models::Node &node);

/// quality_only 模式下这个节点是否值得导出（Rust 的 is_strict_export_candidate）。
bool isStrictExportCandidate(const Models::Node &node);

/// 现场复检候选，只保留仍然可达（需要时还要能完成 TLS 握手）的节点。
/// 候选应当已按 exportScore 降序排好；凑够 want 个就提前收手。
/// 返回顺序是探测完成的顺序，与 Rust 的 JoinSet 一致，不保证输入顺序。
/// 会阻塞当前线程（内部 64 路并发），必须在非 GUI 线程调用，且进程里要有
/// QCoreApplication——等价 Rust 侧的 tokio 任务。
QVector<Models::Node> liveFilterForExport(QVector<Models::Node> nodes, quint32 want,
                                          quint32 timeoutMs);

/// 单节点现场探测：TCP 连得上，且该走 TLS 时握手成功。timeoutMs 直接用，
/// 不再抬到 300ms（抬下限是 Rust live_filter_for_export 那层做的事）。
/// 内部用 QTcpSocket/QSslSocket 的阻塞等待，同样只能在非 GUI 线程调用。
bool liveProbeNode(const Models::Node &node, quint32 timeoutMs);

}
