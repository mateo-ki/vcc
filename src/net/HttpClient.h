#pragma once

#include <QByteArray>
#include <functional>
#include <QList>
#include <QPair>
#include <QString>

#include <atomic>
#include <memory>

#include "core/Result.h"

namespace Net {

/// 被调用方主动取消的请求，Net::send 返回这条错误（和 "连接超时" / "响应头超时"
/// 同一类哨兵，调用方按字符串认）。Rust 那边 abort_all 掉的腿根本不会返回结果，
/// 所以我们这边也必须让"取消"和"失败"可区分 —— 取消不许进失败清单。
inline const QString &cancelledError()
{
    static const QString text = QString::fromUtf8("已取消");
    return text;
}

/// 取消令牌。语义对齐 Rust 的 `JoinHandle::abort`：设一次标志，正在跑的 send 会在
/// 一个轮询周期（40ms）内 abort 自己的 reply 并返回 cancelledError()。
///
/// 为什么是"原子标志 + send 内部轮询"而不是跨线程 `invokeMethod(reply, abort)`：
/// reply 归那条工作线程所有，跨线程攥它的裸指针正是内存审计刚修掉的那类悬垂
/// （send 已经返回、指针还被人拿着）。标志则只是个 shared_ptr，谁拿着都不会错。
struct CancelFlag {
    /// mutable：这是个"外部置位、所有者只读"的信号量，const 成员函数也要能按它。
    mutable std::atomic<bool> cancelled{false};

    void cancel() const { cancelled.store(true, std::memory_order_relaxed); }
    bool isCancelled() const { return cancelled.load(std::memory_order_relaxed); }
};

using CancelToken = std::shared_ptr<CancelFlag>;

struct Header {
    QString name;
    QString value;
};

struct Request {
    QString url;
    QByteArray method = QByteArrayLiteral("GET");
    QByteArray body;
    QString contentType;
    QList<Header> headers;
    /// 单位 ms。整个请求（含重定向）的总时限。
    int timeoutMs = 30000;
    /// 建连（DNS+TCP+TLS）预算，单位 ms，0 表示不限 —— 对应 reqwest 的 connect_timeout。
    /// 缺省是 0 而不是某个值：Rust 那边只有显式设过的客户端才有这一道限制（collector 3s、
    /// geo 6s、内核下载…），默认客户端（IP 风险探针、控制器 /configs/geo）没有，
    /// 所以谁要谁自己设。
    int connectTimeoutMs = 0;
    /// 只等响应头的预算，单位 ms，0 表示不分段（沿用 timeoutMs）。对应 Rust 的
    /// `tokio::time::timeout(header_budget, request.send())`：头到了就把时间让给读体。
    int headerTimeoutMs = 0;
    /// 响应体块间空闲预算，单位 ms，0 表示不监控。对应 Rust 的 BODY_IDLE_TIMEOUT：
    /// 头已到但流断死的对端，只花这块时间而不是整个 timeoutMs。
    int bodyIdleTimeoutMs = 0;
    /// 为空表示这条请求不可取消（绝大多数调用方）。只有并发竞速那种"赢家出现后
    /// 其余腿就该放手"的场景才传（见 Collector::raceCandidates）。
    CancelToken cancel;
    bool followRedirects = true;
    /// 形如 `http://127.0.0.1:9980`；为空表示直连。
    QString proxyUrl;
    /// 条件请求：非空才发对应头。
    QString ifNoneMatch;
    QString ifModifiedSince;
    /// 每个调用方用的 UA 不同（订阅 / telegram / geo / 内核更新各自一个），
    /// 空则不发这个头。取值必须与 Rust 侧一致，有些服务器按 UA 放行。
    QString userAgent;
    /// 最多跟随多少跳重定向（Rust 侧 reqwest 默认 10）。
    int maxRedirects = 10;

    /// 非空 = 这一次当**下载**用：响应体边收边写进这个路径，Response.body 留空。
    /// 几十 MB 的安装包不该整块进内存，而且只有流式写的时候"进度"才是真收了多少。
    /// 任何失败（超时/取消/传输错/非 2xx）都会把写了一半的文件删掉，不留能双击的残包。
    QString downloadToPath;
    /// 下载进度：已收字节、总字节（对端没给 Content-Length 时总字节为 0）。
    /// 只在调用 send() 的那条线程里被回调，别在里面直接碰 widget。
    std::function<void(qint64, qint64)> downloadProgress;
};

struct Response {
    int status = 0;
    QByteArray body;
    QList<Header> headers;
    QString finalUrl;
    /// HTTP 层之外失败时才填（DNS/连接/TLS/超时）。
    QString error;

    QString headerValue(const QString &name) const;
    bool isSuccess() const { return status >= 200 && status < 300; }
};

/// 阻塞式请求：只在 worker 线程里调用（GUI 线程走 sendAsync）。
/// 网络层失败返回 Err（对应 Rust 的 reqwest Error），HTTP 4xx/5xx 是正常的 Response。
Result<Response> send(const Request &request);

/// 是否需要重试：连接类错误、超时、408/429/5xx。对应 collector 的重试判定。
bool isRetryable(const Result<Response> &result);

}
