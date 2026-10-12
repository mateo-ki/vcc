#pragma once

#include "proxy/ConfigTypes.h"

#include <QVector>

namespace Gui {

/// 分流规则栏的纯文案面。对应 Rust `gui/widgets.rs` 的 routing_mode_description /
/// routing_mode_detail / rule_action_label / rule_matcher_label，以及
/// `gui/types.rs` 的 RuleDraftKind（label / needs_value / matcher）。
/// 中文一律取 tools/gen-cjk-codepoints.mjs 生成的 UiText，手打就不算 1:1。

/// Rust `RuleDraftKind`：新增规则下拉里的十种匹配条件。
/// 注意它和 Proxy::RuleMatcher::Kind 不是一回事 —— Tcp/Udp 两种其实是同一个
/// Network 变体的两个协议，所以必须单独有这张表，不能拿 Kind 直接排。
enum class RuleDraftKind {
    Domain,
    DomainSuffix,
    GeoSite,
    GeoIp,
    IpCidr,
    DestinationPort,
    ProcessName,
    Tcp,
    Udp,
    Quic,
};

/// Rust `RuleDraftKind::ALL` 的顺序，界面按它排下拉项。
const QVector<RuleDraftKind> &ruleDraftAll();

QString ruleDraftLabel(RuleDraftKind kind);
/// Rust `needs_value`：TCP / UDP / QUIC 三种没有匹配值。
bool ruleDraftNeedsValue(RuleDraftKind kind);
/// Rust `matcher`：值先 trim，空值交给 toMihomoRule 去拒。
Proxy::RuleMatcher ruleDraftMatcher(RuleDraftKind kind, const QString &value);

QString routingModeDescription(Proxy::RoutingMode mode);
QString routingModeDetail(Proxy::RoutingMode mode);
QString ruleActionText(Proxy::RuleAction action);
QString ruleMatcherText(const Proxy::RuleMatcher &matcher);

}
