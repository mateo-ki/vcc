#include "gui/RulesPanel.h"

#include "gui/UiText.h"

namespace Gui {

const QVector<RuleDraftKind> &ruleDraftAll()
{
    static const QVector<RuleDraftKind> all = {
        RuleDraftKind::Domain,        RuleDraftKind::DomainSuffix, RuleDraftKind::GeoSite,
        RuleDraftKind::GeoIp,         RuleDraftKind::IpCidr,       RuleDraftKind::DestinationPort,
        RuleDraftKind::ProcessName,   RuleDraftKind::Tcp,          RuleDraftKind::Udp,
        RuleDraftKind::Quic,
    };
    return all;
}

QString ruleDraftLabel(RuleDraftKind kind)
{
    using K = RuleDraftKind;
    switch (kind) {
    case K::Domain:
        return UiText::ruleDraftDomain();
    case K::DomainSuffix:
        return UiText::ruleDraftDomainSuffix();
    case K::GeoSite:
        return QStringLiteral("GeoSite");
    case K::GeoIp:
        return QStringLiteral("GeoIP");
    case K::IpCidr:
        return QStringLiteral("IP-CIDR");
    case K::DestinationPort:
        return UiText::ruleDraftDestinationPort();
    case K::ProcessName:
        return UiText::ruleDraftProcessName();
    case K::Tcp:
        return QStringLiteral("TCP");
    case K::Udp:
        return QStringLiteral("UDP");
    case K::Quic:
        return QStringLiteral("QUIC");
    }
    return QString();
}

bool ruleDraftNeedsValue(RuleDraftKind kind)
{
    return kind != RuleDraftKind::Tcp && kind != RuleDraftKind::Udp
        && kind != RuleDraftKind::Quic;
}

Proxy::RuleMatcher ruleDraftMatcher(RuleDraftKind kind, const QString &value)
{
    using K = RuleDraftKind;
    using M = Proxy::RuleMatcher;
    const QString trimmed = value.trimmed();
    switch (kind) {
    case K::Domain:
        return M::simple(M::Kind::Domain, trimmed);
    case K::DomainSuffix:
        return M::simple(M::Kind::DomainSuffix, trimmed);
    case K::GeoSite:
        return M::simple(M::Kind::GeoSite, trimmed);
    case K::GeoIp:
        return M::simple(M::Kind::GeoIp, trimmed);
    case K::IpCidr:
        return M::simple(M::Kind::IpCidr, trimmed);
    case K::DestinationPort:
        return M::simple(M::Kind::DestinationPort, trimmed);
    case K::ProcessName:
        return M::simple(M::Kind::ProcessName, trimmed);
    case K::Tcp:
        return M::makeNetwork(Proxy::NetworkProtocol::Tcp);
    case K::Udp:
        return M::makeNetwork(Proxy::NetworkProtocol::Udp);
    case K::Quic:
        return M::makeQuic();
    }
    return M::simple(M::Kind::Domain, trimmed);
}

QString routingModeDescription(Proxy::RoutingMode mode)
{
    using M = Proxy::RoutingMode;
    switch (mode) {
    case M::Rule:
        return UiText::routingModeDescRule();
    case M::ChinaDirect:
        return UiText::routingModeDescChinaDirect();
    case M::GfwList:
        return UiText::routingModeDescGfwList();
    case M::Global:
        return UiText::routingModeDescGlobal();
    case M::Direct:
        return UiText::routingModeDescDirect();
    }
    return QString();
}

QString routingModeDetail(Proxy::RoutingMode mode)
{
    using M = Proxy::RoutingMode;
    switch (mode) {
    case M::Rule:
        return UiText::routingModeDetailRule();
    case M::ChinaDirect:
        return UiText::routingModeDetailChinaDirect();
    case M::GfwList:
        return UiText::routingModeDetailGfwList();
    case M::Global:
        return UiText::routingModeDetailGlobal();
    case M::Direct:
        return UiText::routingModeDetailDirect();
    }
    return QString();
}

QString ruleActionText(Proxy::RuleAction action)
{
    using A = Proxy::RuleAction;
    switch (action) {
    case A::Direct:
        return UiText::ruleActionDirect();
    case A::Proxy:
        return UiText::ruleActionProxy();
    case A::Reject:
        return UiText::ruleActionReject();
    }
    return QString();
}

QString ruleMatcherText(const Proxy::RuleMatcher &matcher)
{
    using K = Proxy::RuleMatcher::Kind;
    switch (matcher.kind) {
    case K::Domain:
        return UiText::matcherLabelDomain().arg(matcher.value);
    case K::DomainSuffix:
        return UiText::matcherLabelDomainSuffix().arg(matcher.value);
    case K::GeoSite:
        return UiText::matcherLabelGeoSite().arg(matcher.value);
    case K::GeoIp:
        return UiText::matcherLabelGeoIp().arg(matcher.value);
    case K::IpCidr:
        return UiText::matcherLabelIpCidr().arg(matcher.value);
    case K::DestinationPort:
        return UiText::matcherLabelDestinationPort().arg(matcher.value);
    case K::ProcessName:
        return UiText::matcherLabelProcessName().arg(matcher.value);
    case K::Network:
        return matcher.network == Proxy::NetworkProtocol::Tcp
            ? UiText::matcherLabelNetworkTcp()
            : UiText::matcherLabelNetworkUdp();
    case K::Quic:
        return UiText::matcherLabelQuic();
    }
    return matcher.value;
}

}
