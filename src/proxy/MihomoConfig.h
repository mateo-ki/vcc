#pragma once

#include <QString>
#include <QVector>

#include "core/JsonUtil.h"
#include "core/Result.h"
#include "kernel/KernelTypes.h"
#include "models/Node.h"
#include "proxy/ConfigTypes.h"

namespace Proxy {

enum class ProxyMode {
    System,
    Tun,
};

QString proxyModeToText(ProxyMode mode);
ProxyMode proxyModeFromText(const QString &text);

enum class ProxyState {
    Stopped,
    Starting,
    Running,
    Degraded,
    Stopping,
    Failed,
};

QString proxyStateToText(ProxyState state);

/// Degraded：本地端口已就绪但外部连通性验证失败。系统代理会话继续保留，
/// 界面必须把它和 Running 区分开——只看 state 挂绿勾会重演"内核日志全是 i/o
/// timeout 而界面还绿着"。
bool stateHasLocalProxy(ProxyState state);

/// 内核内自动选路用的两个组名。
extern const QString kUrlTestGroup;
extern const QString kFinalGroup;

/// url-test 的判活节奏。原来是 `interval: 300`：一台节点从坏掉到被避开最长要等 5 分钟
/// —— 2026-10-05 实测那台 SG 节点连续 17 分钟 502，内核每 300 秒才重测它一次。
/// 调到 60/3000/30 的代价：每 60 秒对每个候选各发一次 HEAD 请求（几十 KB 级，CPU 可忽略）；
/// 收益：坏节点 1 分钟内被重测，tolerance 30 让"换到明显更快的那台"更少犹豫。
inline constexpr qint64 kUrlTestIntervalSecs = 60;
inline constexpr qint64 kUrlTestToleranceMs = 30;
inline constexpr qint64 kUrlTestTimeoutMs = 3000;

inline constexpr quint16 kDefaultUrlTestSize = 20;

/// 代理运行配置。持久化为 proxy_settings.config_json，字段名与 serde 完全一致；
/// `controller_secret` 与 `url_test_nodes` 是 `#[serde(skip)]`，不进 JSON。
struct MihomoConfigOptions {
    ProxyMode mode = ProxyMode::System;
    quint16 mixedPort = 9980;
    quint16 controllerPort = 9090;
    bool allowLan = false;
    TunConfig tun;
    DnsConfig dns;
    RoutingMode routingMode = RoutingMode::Rule;
    QVector<TrafficRule> rules;
    Kernel::Preference kernelPreference = Kernel::Preference::Auto;
    /// mihomo `external-controller` 的一次性密钥，不落盘、不跨进程共享。
    QString controllerSecret;
    /// 内核内自动选路：把测速前 N 名一起写进配置，交给 mihomo 的 url-test 组按延迟
    /// 自己切换。切换就不再需要重写配置 + 热重载（那会掐断所有已建立的连接）。
    /// 仅 Mihomo 内核支持；sing-box / xray 仍按单节点生成。
    bool urlTest = false;
    /// 写进 url-test 组的节点上限（含当前节点）。
    quint16 urlTestSize = kDefaultUrlTestSize;
    /// 终端代理：启动时把 HTTP_PROXY / HTTPS_PROXY / ALL_PROXY / NO_PROXY 写进
    /// HKCU\Environment（Git、curl、npm、pip 只认这些，不认 WinINet 系统代理）。
    bool terminalProxy = false;
    /// 候选节点（已转成 clash proxy 对象）：启动前由界面按最新测速填入，不持久化。
    QVector<Json> urlTestNodes;

    Status validate() const;

    Json toJson() const;
    static MihomoConfigOptions fromJson(const Json &value);
    /// 对应 `Database::get_proxy_config`：解析已保存的 JSON，迁移老默认值后校验。
    static Result<MihomoConfigOptions> fromSavedJsonText(const QString &jsonText);
    /// 对应 `Database::update_proxy_config`：先校验再序列化。
    Result<QString> toSavedJsonText() const;
};

struct ProxyRuntimeStatus {
    ProxyState state = ProxyState::Stopped;
    std::optional<Kernel::Kind> kernel;
    ProxyMode mode = ProxyMode::System;
    QString nodeId;
    quint16 mixedPort = 9980;
    quint16 controllerPort = 9090;
    /// 与 MihomoConfigOptions::controllerSecret 同步，供 GUI 调用控制器时使用。
    QString controllerSecret;
    QString message;
    bool localProxyReady = false;
    bool systemProxyEnabled = false;
    QString lastError;

    static ProxyRuntimeStatus stopped(const MihomoConfigOptions &options);
    bool localProxyActive() const;
    bool configEditable() const;
    bool kernelInUse(Kernel::Kind kind) const;

    Json toJson() const;
};

/// 生成 mihomo 配置文档（对应 build_mihomo_config）。
Result<Json> buildMihomoConfig(const Models::Node &node, MihomoConfigOptions options);

/// 错误文本是否表示"节点连不通"（而不是启动失败之类的其它问题）。
bool isNodeConnectivityFailure(const QString &message);

}
