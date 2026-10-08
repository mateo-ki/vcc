#pragma once

#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

#include "core/Result.h"

// 节点落地 IP 的"欺诈/风控"画像：延迟低不等于能用，OpenAI / Google / Claude 这类站点会按
// ASN 风控，机房 IP 即使连通也更容易掉登录、封号或被判定为自动化流量。单靠 ipinfo 的 org
// 关键字会大量漏判，所以双源合并：ipinfo.io + ip-api.com，任一判机房就算机房。
namespace IpRisk {

/// 对应 Rust `struct ip_risk::IpRisk`。这里叫 `Risk`：C++ 里成员与命名空间同名会让
/// `using namespace IpRisk;` 之后的 `IpRisk::parse(...)` 失效（名字被结构体占掉）。
struct Risk {
    QString ip;
    /// 查询时用的节点 server 字段：域名节点会先解析，这里留着说明"查的是谁"。
    QString hostname;
    QString country;
    QString org;
    /// 机房 / 云厂商 IP（最容易触发风控）。
    bool datacenter = false;
    /// ip-api 把它标成公开代理 / VPN 出口（VPN Gate 这类一定为真）。
    bool publicProxy = false;
    QString verdict;

    /// `ip · 国家 · ASN/运营商 · 类型`，对应 Rust `IpRisk::headline()`。
    QString headline() const;
};

/// 对应 Rust `parse`（纯函数，可单测）：`apiBody` 为 null QString 表示没拿到 ip-api 的响应，
/// 此时退回 ipinfo + 关键字这一路。
Result<Risk> parse(const QString &hostname, const QString &ip, const QString &body,
                   const QString &apiBody);

// ---- 请求构造：纯函数，测试据此核对 URL，不发网络 ----

/// `https://ipinfo.io/{address}/json`
QString ipinfoUrl(const QString &address);
/// `http://ip-api.com/json/{address}?fields=status,hosting,proxy,mobile,as,isp`
QString ipApiUrl(const QString &address);
/// 两个回显地址：`https://api.ipify.org`、`https://ipinfo.io/ip`。
const QStringList &egressEchoUrls();
/// ipinfo 的查询 `what` 标签（Rust 传的是 &str，这里就是字面量）。
QString whatIpinfo();
/// ip-api 的查询 `what` 标签。
QString whatIpApi();

/// 文本是不是 IP：是则返回规范化后的文本（Rust 的 `IpAddr::to_string()`），否则 null QString。
QString addressText(const QString &text);

/// 对应 Rust `fetch_text`：整个模块**唯一**发 HTTP 请求的地方（GET，8 秒总时限，
/// 不带 User-Agent，proxyUrl 为 null/空表示直连）。
Result<QString> fetchText(const QString &url, const QString &what, const QString &proxyUrl);

/// 对应 Rust `resolve_first`：走 DNS（不是 HTTP），优先 IPv4。返回规范化后的 IP 文本。
/// 域名前置 / ECH / CDN 中转的节点，这里解析到的往往是前端地址而不是真实落地出口。
Result<QString> resolveFirst(const QString &host);

/// 对应 Rust `lookup_egress` 里 `reqwest::Proxy::all(proxy_url)` 的那次校验：
/// 地址没有 scheme 或没有主机名时报"代理地址无效"（和 Rust 一样在发第一个请求之前就失败）。
Status checkProxyUrl(const QString &proxyUrl);

/// 纯函数：把回显响应体按 Rust 的 `trim().split_whitespace().next().unwrap_or_default()` 取词，
/// 再按 `parse::<IpAddr>()` 判断是不是 IP。`addressText` 为 null 表示不是 IP。
struct EgressCandidate
{
    QString text;
    QString addressText;
};
EgressCandidate egressCandidate(const QString &body);

/// 对应 Rust `classify_ip` / `lookup` / `lookup_egress`：这三个会发请求，测试请用下面的注入版。
Result<Risk> classifyIp(const QString &label, const QString &address);
Result<Risk> lookup(const QString &server);
Result<Risk> lookupEgress(const QString &proxyUrl);

/// Rust 侧靠"传 &Client"来注入，这里改成注入取文本/解析的回调，测试因此完全不联网。
using Fetcher = std::function<Result<QString>(const QString &url, const QString &what,
                                              const QString &proxyUrl)>;
using Resolver = std::function<Result<QString>(const QString &host)>;

Result<Risk> classifyIpWith(const Fetcher &fetch, const QString &label, const QString &address);
Result<Risk> lookupWith(const Fetcher &fetch, const Resolver &resolve, const QString &server);
Result<Risk> lookupEgressWith(const Fetcher &fetch, const QString &proxyUrl);

}
