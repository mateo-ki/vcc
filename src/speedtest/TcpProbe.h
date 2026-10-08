#pragma once

#include <QHostAddress>
#include <QString>
#include <QVector>

#include <memory>
#include <optional>

#include "core/Result.h"

class QTcpSocket;

/// TCP 连通性/下载探测的底层管道，对应 Rust speedtest/mod.rs 里的自由函数
/// connect_fast / connect_resolved / measure_tcp_download / looks_like_public_website，
/// 以及 tokio 的 lookup_host。
///
/// 【全部是阻塞调用】只能在 worker 线程上跑（上层用 QtConcurrent/QThreadPool 进来）：
/// 这些函数内部会临时起一个局部事件循环来复刻 tokio 的 timeout，GUI 线程绝对不许调用。
namespace Speedtest {

/// 已经建立的连接 + 握手耗时，对应 Rust 的 `(TcpStream, i64)`。
struct Connected {
    std::unique_ptr<QTcpSocket> socket;
    qint64 latencyMs = 0;
};

/// 一段下载探测的结果，对应 Rust 的 `(Option<f64>, String)`（速率 + 说明文本）。
struct DownloadMeasurement {
    std::optional<double> kbps;
    QString detail;
};

/// 对应 `tokio::net::lookup_host("{host}:{port}")`：解析失败/超时/为空都是错误，
/// 地址按 IPv4 优先排序（免费节点一堆坏 AAAA）。timeoutMs 是 DNS 的预算。
Result<QVector<QHostAddress>> resolveAddresses(const QString &host, qint64 timeoutMs);

/// 解析 + 连接共用一个总预算（批量扫描更快），对应 Rust 的 `connect_fast`。
Result<Connected> connectFast(const QString &host, int port, qint64 timeoutMs);

/// 并发试所有已解析地址、先到先得，对应 Rust 的 `connect_resolved`。
Result<Connected> connectResolved(const QVector<QHostAddress> &addresses, int port,
                                  qint64 timeoutMs);

/// 尽力而为的 HTTP GET 下载探测（加密代理大多不回应，测不到就留空）。
/// 对应 Rust 的 `measure_tcp_download`，接管 socket 但在函数返回前不关闭它。
DownloadMeasurement measureTcpDownload(QTcpSocket &socket, const QString &host, qint64 downloadMs);

/// 响应头长得像普通网站（HTTP/1.x + 常见状态码 + HTML/JSON 特征）就不把它当下载吞吐。
/// 对应 Rust 的 `looks_like_public_website`。
bool looksLikePublicWebsite(const QByteArray &head);

}
