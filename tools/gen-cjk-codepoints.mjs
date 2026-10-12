import fs from 'node:fs';

// 从 Rust 源里抽取中文提示文本，生成纯 ASCII 的 C++ 码点表：
//   node tools/gen-cjk-codepoints.mjs
// 这些文本会进入错误消息和界面，必须与 Rust 逐字一致，所以不允许我手打。
// 每条用一个 ASCII 锚点定位，再在附近几行里找那个非 ASCII 字面量；
// 找到 0 个或多个就报错退出，绝不猜。
const RUST = 'D:/project/rust/vs/vpn-parser/src';

const outputs = {
  sysproxy: {
    header: 'D:/project/cpp/vcc/src/sysproxy/Messages.h',
    source: 'D:/project/cpp/vcc/src/sysproxy/Messages.cpp',
    ns: 'SysProxy::Messages',
    include: '#include "sysproxy/Messages.h"',
  },
  geo: {
    header: 'D:/project/cpp/vcc/src/geo/GeoMessages.h',
    source: 'D:/project/cpp/vcc/src/geo/GeoMessages.cpp',
    ns: 'Geo::Messages',
    include: '#include "geo/GeoMessages.h"',
  },
  gui: {
    header: 'D:/project/cpp/vcc/src/gui/ThemeNames.h',
    source: 'D:/project/cpp/vcc/src/gui/ThemeNames.cpp',
    ns: 'Gui::ThemeNames',
    include: '#include "gui/ThemeNames.h"',
  },
  uitext: {
    header: 'D:/project/cpp/vcc/src/gui/UiText.h',
    source: 'D:/project/cpp/vcc/src/gui/UiText.cpp',
    ns: 'Gui::UiText',
    include: '#include "gui/UiText.h"',
  },
};

const entries = [
  ['platformUnsupported', 'system_proxy/mod.rs', 'Err(anyhow!("', 0, 'nonAscii', 'sysproxy'],
  ['wininetNotifyFailed', 'system_proxy/mod.rs', 'Err(anyhow!("', 4, 'nonAscii', 'sysproxy'],
  ['regExitCode', 'system_proxy/mod.rs', 'format!("reg.exe', 0, 'nonAscii', 'sysproxy'],
  ['deleteValueFailed', 'system_proxy/mod.rs', 'reg_failure_detail(&output)', 0, 'nonAscii', 'sysproxy'],
  ['modifyFailed', 'system_proxy/mod.rs', 'let detail = reg_failure_detail', 0, 'nonAscii', 'sysproxy'],
  ['writtenPrefix', 'system_proxy/terminal.rs', 'parts.push(format!(', 0, 'nonAscii', 'sysproxy'],
  ['removedPrefix', 'system_proxy/terminal.rs', 'parts.push(format!(', 1, 'nonAscii', 'sysproxy'],
  ['untouchedPrefix', 'system_proxy/terminal.rs', 'parts.push(format!(', 2, 'nonAscii', 'sysproxy'],
  ['noChange', 'system_proxy/terminal.rs', 'if parts.is_empty()', 0, 'nonAscii', 'sysproxy'],
  ['joiner', 'system_proxy/terminal.rs', 'parts.join(', 0, 'nonAscii', 'sysproxy'],
  ['writeFailed', 'system_proxy/terminal.rs', 'String::from_utf8_lossy(&output.stdout)', 1, 'nonAscii', 'sysproxy'],
  ['removeFailed', 'system_proxy/terminal.rs', 'String::from_utf8_lossy(&output.stdout)', 2, 'nonAscii', 'sysproxy'],
  ['alreadyYours', 'system_proxy/terminal.rs', 'if !is_ours(&existing)', 0, 'nonAscii', 'sysproxy'],
  ['windowsOnly', 'system_proxy/terminal.rs', 'pub fn enable(_port: u16)', 0, 'nonAscii', 'sysproxy'],
  ['noProxyValue', 'system_proxy/terminal.rs', 'pub const NO_PROXY_VALUE', 0, 'any', 'sysproxy'],

  // ---- geo.rs：分流数据库下载。半截库会让内核起不来，报错文案要一致。 ----
  ['geoTruncated', 'geo.rs', 'if (bytes.len() as u64) < MIN_GEO_BYTES {', 0, 'nonAscii', 'geo'],
  ['geoHtmlBody', 'geo.rs', 'if bytes.starts_with(b"<") {', 0, 'nonAscii', 'geo'],
  ['geoBadMagic', 'geo.rs', 'if bytes[0] != 0x0a {', 0, 'nonAscii', 'geo'],
  ['geoAlreadyReady', 'geo.rs', 'report.push(format!(', 0, 'nonAscii', 'geo'],
  ['geoUpdated', 'geo.rs', 'report.push(format!(', 1, 'nonAscii', 'geo'],
  ['geoAllSourcesFailed', 'geo.rs', 'if !done {', 0, 'nonAscii', 'geo'],
  ['geoReadBodyFailed', 'geo.rs', 'Ok(body) => body,', 0, 'nonAscii', 'geo'],

  // ---- gui/theme.rs：五套主题名，设置页直接显示。 ----
  ['softCobalt', 'gui/theme.rs', 'Theme { id: "soft-cobalt"', 0, 'nonAscii', 'gui'],
  ['softTeal', 'gui/theme.rs', 'Theme { id: "soft-teal"', 0, 'nonAscii', 'gui'],
  ['softTerracotta', 'gui/theme.rs', 'Theme { id: "soft-terracotta"', 0, 'nonAscii', 'gui'],
  ['softIndigo', 'gui/theme.rs', 'Theme { id: "soft-indigo"', 0, 'nonAscii', 'gui'],
  ['softViolet', 'gui/theme.rs', 'Theme { id: "soft-violet"', 0, 'nonAscii', 'gui'],

  // ---- gui/views/sidebar.rs：侧栏品牌行与五个导航项。 ----
  ['brandSubtitle', 'gui/views/sidebar.rs', '.small().color(theme::sidebar_muted()));', 0, 'nonAscii', 'uitext'],
  ['navConsole', 'gui/views/sidebar.rs', 'Tab::Proxy, Icon::', 0, 'nonAscii', 'uitext'],
  ['navNodePool', 'gui/views/sidebar.rs', 'Tab::Nodes, Icon::', 0, 'nonAscii', 'uitext'],
  ['navSubscriptions', 'gui/views/sidebar.rs', 'Tab::Sources, Icon::', 0, 'nonAscii', 'uitext'],
  ['navSystemSettings', 'gui/views/sidebar.rs', 'Tab::Settings, Icon::', 0, 'nonAscii', 'uitext'],
  ['navExternalApi', 'gui/views/sidebar.rs', 'Tab::Api, Icon::', 0, 'nonAscii', 'uitext'],

  // ---- gui/views/proxy.rs：控制台六个分栏。锚点取 label 自己那行附近，
  // 因为"活跃连接"是 conn_label 算出来的，不在 ProxyPanel:: 那一行上。 ----
  ['panelNodes', 'gui/views/proxy.rs', 'ProxyPanel::Nodes,', 0, 'nonAscii', 'uitext'],
  ['panelConnections', 'gui/views/proxy.rs', 'let conn_label = if self.active_connections.is_empty()', 0, 'nonAscii', 'uitext'],
  ['panelConnectionsCounted', 'gui/views/proxy.rs', 'self.active_connections.len())', 0, 'nonAscii', 'uitext', 0, 0, true],

  // ---- 状态卡健康条 + 出口切换记录（widgets.rs / views/proxy.rs 链接 / mod.rs::ui_switch_log）。 ----
  ['healthStripHover', 'gui/widgets.rs', '最近 24 小时出口实测', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['switchLogLink', 'gui/views/proxy.rs', '查看切换记录（今日', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['switchLogTitle', 'gui/mod.rs', 'egui::Window::new("出口切换记录")', 0, 'nonAscii', 'uitext', 0, 0, false],
  ['switchLogSummary', 'gui/mod.rs', '共 {} 条 · 今天', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['switchLogEmpty', 'gui/mod.rs', '还没有切换记录。', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['switchLogHidden', 'gui/mod.rs', '更早的 {hidden} 条未在此显示。', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['switchLogTodayDay', 'gui/mod.rs', 'format!("{day} · 今天")', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['switchLogNoSwitch', 'gui/mod.rs', '"没切成".into()', 0, 'nonAscii', 'uitext', 0, 0, true],

  // ---- 故障自动切换的判定与复检（mod.rs::begin_failover_switch / on_failover_target）。 ----
  ['failoverSearching', 'gui/mod.rs', '替补复检还在进行中', 0, 'nonAscii', 'uitext', 0, 0, false],
  ['failoverNoSecondCandidate', 'gui/mod.rs', '控制台候选里没有第二个可用节点', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['failoverTcpRecheck', 'gui/mod.rs', '正在对 {} 个替补节点做 TCP 复检', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['failoverAllUnreachable', 'gui/mod.rs', '个候选，没有一个端口连得上', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['failoverNoneVerified', 'gui/mod.rs', '库里也找不出第二个测通过的节点', 0, 'nonAscii', 'uitext', 0, 0, false],
  ['failoverSwitchedTo', 'gui/mod.rs', '复检通过 → 已切到', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['failoverButReason', 'gui/mod.rs', '判定该换出口，但', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['failoverMissPrefix', 'gui/mod.rs', '没有可用替补：', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['failoverBreakerBlacklisted', 'gui/mod.rs', '当前节点已被断路器拉黑', 0, 'nonAscii', 'uitext', 0, 0, false],
  ['failoverSeekingBackup', 'gui/mod.rs', '正在寻找替补节点', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['switchReasonAuto', 'gui/mod.rs', 'switch_proxy_node(id, name, "自动切换")', 0, 'nonAscii', 'uitext', 0, 0, false],

  // ---- TUN 模式启动要提权：请求侧与交接文案（mod.rs::start_proxy 的提权分支）。 ----
  ['elevatedRequested', 'gui/mod.rs', '已请求管理员权限，正在切换到提权窗口', 0, 'nonAscii', 'uitext', 0, 0, false],
  ['elevatedRequestFailed', 'gui/mod.rs', '请求管理员权限失败', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['elevatedConfigSerializeFailed', 'gui/mod.rs', '序列化 TUN 配置失败', 0, 'nonAscii', 'uitext', 0, 0, true],

  // ---- 状态卡第二行的实时流量图（widgets.rs::render_traffic_sparkline）。 ----
  ['sparklineWaiting', 'gui/widgets.rs', '等待流量统计', 0, 'nonAscii', 'uitext', 0, 0, false],
  ['sparklineIdle', 'gui/widgets.rs', '代理未运行 · 启动后这里画实时上下行', 0, 'nonAscii', 'uitext', 0, 0, false],
  ['sparklineHover', 'gui/widgets.rs', '秒前 · ↓', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['panelRules', 'gui/views/proxy.rs', 'ProxyPanel::Rules,', 0, 'nonAscii', 'uitext'],
  ['panelNetwork', 'gui/views/proxy.rs', 'ProxyPanel::Network,', 0, 'any', 'uitext'],
  ['panelKernels', 'gui/views/proxy.rs', 'ProxyPanel::Kernels,', 0, 'nonAscii', 'uitext'],
  ['panelLogs', 'gui/views/proxy.rs', 'ProxyPanel::Logs,', 0, 'nonAscii', 'uitext'],

  // ---- gui/widgets.rs::proxy_state_label：状态徽章的文字（颜色在代码里按角色映射）。 ----
  ['stateStopped', 'gui/widgets.rs', 'ProxyState::Stopped => (', 0, 'nonAscii', 'uitext'],
  ['stateStarting', 'gui/widgets.rs', 'ProxyState::Starting => (', 0, 'nonAscii', 'uitext'],
  ['stateRunning', 'gui/widgets.rs', 'ProxyState::Running => (', 0, 'nonAscii', 'uitext'],
  ['stateDegraded', 'gui/widgets.rs', 'ProxyState::Degraded => (', 0, 'nonAscii', 'uitext'],
  ['stateStopping', 'gui/widgets.rs', 'ProxyState::Stopping => (', 0, 'nonAscii', 'uitext'],
  ['stateFailed', 'gui/widgets.rs', 'ProxyState::Failed => (', 0, 'nonAscii', 'uitext'],

  // ---- gui/views/proxy.rs：「预备」徽章的三种说法。 ----
  ['preparedFailed', 'gui/views/proxy.rs', '} else if self.prepare_error.is_some() {', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['preparedDownloading', 'gui/views/proxy.rs', 'crate::gui::prepare::MAX_AUTO_ATTEMPTS', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['prepared', 'gui/views/proxy.rs', '(text, theme::accent())', 0, 'nonAscii', 'uitext'],

  // ---- gui/views/proxy.rs：可用节点栅格的筛选条。 ----
  ['regionLabel', 'gui/views/proxy.rs', 'let regions = [', 0, 'nonAscii', 'uitext'],
  ['regionAll', 'gui/views/proxy.rs', '("all", ', 0, 'nonAscii', 'uitext'],
  ['regionHK', 'gui/views/proxy.rs', '("HK", ', 0, 'nonAscii', 'uitext'],
  ['regionJP', 'gui/views/proxy.rs', '("JP", ', 0, 'nonAscii', 'uitext'],
  ['regionUS', 'gui/views/proxy.rs', '("US", ', 0, 'nonAscii', 'uitext'],
  ['regionSG', 'gui/views/proxy.rs', '("SG", ', 0, 'nonAscii', 'uitext'],
  ['regionTW', 'gui/views/proxy.rs', '("TW", ', 0, 'nonAscii', 'uitext'],
  ['regionKR', 'gui/views/proxy.rs', '("KR", ', 0, 'nonAscii', 'uitext'],
  ['regionOther', 'gui/views/proxy.rs', '("OTHER", ', 0, 'nonAscii', 'uitext'],
  ['sortLabel', 'gui/views/proxy.rs', 'let is_lat = self.proxy_sort_by ==', 0, 'nonAscii', 'uitext'],
  ['sortByLatency', 'gui/views/proxy.rs', 'selectable_icon_btn(Icon::Lightning,', 0, 'nonAscii', 'uitext'],
  ['sortByName', 'gui/views/proxy.rs', 'selectable_icon_btn(Icon::Sort,', 0, 'nonAscii', 'uitext'],
  ['nodeCountTotal', 'gui/views/proxy.rs', 'self.proxy_nodes.len())', 0, 'nonAscii', 'uitext'],
  ['searchHint', 'gui/views/proxy.rs', '.hint_text(', 0, 'nonAscii', 'uitext'],

  // ---- 行右键菜单与切换原因（缺一条就得自己编中文，那是禁止的）。 ----
  ['menuDetails', 'gui/views/proxy.rs', 'self.show_node(node_id.clone())', 0, 'nonAscii', 'uitext'],
  ['menuSpeedtest', 'gui/views/proxy.rs', 'self.speedtest_one(node_id.clone())', 0, 'nonAscii', 'uitext'],
  ['menuIpRisk', 'gui/views/proxy.rs', 'self.check_ip_risk_for(&node_id)', 0, 'nonAscii', 'uitext'],
  ['menuLockEgress', 'gui/views/proxy.rs', 'locked = Some((node_id.clone(), node_name.clone()))', 0, 'nonAscii', 'uitext'],
  ['switchReasonManual', 'gui/views/proxy.rs', 'self.switch_proxy_node(id, name, ', 0, 'nonAscii', 'uitext'],

  // ---- gui/widgets.rs:节点行右侧的延迟文案与状态文案。 ----
  ['latencyProxyFailed', 'gui/widgets.rs', '(Some(false), Some(GOOGLE_PROXY_TEST_KIND)) => RichText::new(', 0, 'nonAscii', 'uitext'],
  ['latencyTcpFailed', 'gui/widgets.rs', '(Some(false), _) => RichText::new(', 0, 'nonAscii', 'uitext'],
  ['latencyUntested', 'gui/widgets.rs', '(None, _) => RichText::new(', 0, 'nonAscii', 'uitext'],
  ['kernelNameFallback', 'gui/widgets.rs', 'None => "', 0, 'nonAscii', 'uitext'],
  ['switchingLabel', 'gui/widgets.rs', 'icon_label(ui, Icon::Lightning, 12.0, theme::warning());', 0, 'nonAscii', 'uitext'],
  ['emptyNodeGrid', 'gui/views/proxy.rs', 'if view.rows.is_empty() {', 0, 'nonAscii', 'uitext'],

  ['egressLockedStatus', 'gui/views/proxy.rs', 'self.status = format!(', 0, 'nonAscii', 'uitext'],

  ['kernelSectionTitle', 'gui/views/proxy.rs', 'v2rayN', 0, 'at', 'uitext', -1, 0],
  ['kernelSectionHint', 'gui/views/proxy.rs', 'v2rayN', 0, 'nonAscii', 'uitext'],
  ['kernelStorageHint', 'gui/views/proxy.rs', 'GitHub Release', 0, 'nonAscii', 'uitext'],
  ['kernelCheckUpdates', 'gui/views/proxy.rs', 'add_enabled(!self.kernel_busy, egui::Button::new(', 0, 'nonAscii', 'uitext'],
  ['kernelCheckingStatus', 'gui/views/proxy.rs', 'self.reload_kernels();', 0, 'at', 'uitext', 1, 0],
  ['kernelCancelDownload', 'gui/views/proxy.rs', 'if self.kernel_busy && ui.add(danger_btn(', 0, 'nonAscii', 'uitext'],
  ['kernelCancellingStatus', 'gui/views/proxy.rs', 'self.kernel_cancel.store(true, Ordering::Relaxed)', 0, 'at', 'uitext', 1, 0],
  ['kernelColumnKind', 'gui/views/proxy.rs', '[kind_width, 20.0],', 0, 'at', 'uitext', 1, 0],
  ['kernelColumnStatus', 'gui/views/proxy.rs', '[status_width, 20.0],', 0, 'at', 'uitext', 1, 0],
  ['kernelColumnVersion', 'gui/views/proxy.rs', '[version_width, 20.0],', 0, 'at', 'uitext', 1, 0],
  ['kernelColumnLatest', 'gui/views/proxy.rs', '[latest_width, 20.0],', 0, 'at', 'uitext', 1, 0],
  ['kernelColumnPath', 'gui/views/proxy.rs', '[path_width, 20.0],', 0, 'at', 'uitext', 1, 0],
  ['kernelColumnAction', 'gui/views/proxy.rs', '[action_width, 20.0],', 0, 'at', 'uitext', 1, 0],
  ['kernelStateAvailable', 'gui/views/proxy.rs', 'RichText::new(if kernel.available {', 0, 'at', 'uitext', 1, 0],
  ['kernelStateMissing', 'gui/views/proxy.rs', 'RichText::new(if kernel.available {', 0, 'at', 'uitext', 3, 0],
  ['kernelLatestChecking', 'gui/views/proxy.rs', 'release.latest_version.as_deref()', 0, 'at', 'uitext', 1, 0],
  ['kernelActionUpdate', 'gui/views/proxy.rs', 'let label = if kernel.available {', 0, 'at', 'uitext', 0, 0],
  ['kernelActionDownload', 'gui/views/proxy.rs', 'let label = if kernel.available {', 0, 'at', 'uitext', 0, 1],
  ['kernelHoverInUse', 'gui/views/proxy.rs', 'on_hover_text(if self.proxy_status.kernel_in_use', 0, 'at', 'uitext', 1, 0],
  ['kernelHoverAsset', 'gui/views/proxy.rs', 'SHA-256', 0, 'nonAscii', 'uitext'],
  ['kernelProgressFormat', 'gui/views/proxy.rs', 'progress.kind.display_name(),', 0, 'at', 'uitext', -1, 0],
  ['kernelDownloadFormat', 'gui/views/proxy.rs', 'format_bytes(progress.downloaded),', 0, 'at', 'uitext', -1, 0],
  ['routingModeDescRule', 'gui/widgets.rs', 'pub fn routing_mode_description', 0, 'at', 'uitext', 2, 0],
  ['routingModeDescChinaDirect', 'gui/widgets.rs', 'pub fn routing_mode_description', 0, 'at', 'uitext', 3, 0],
  ['routingModeDescGfwList', 'gui/widgets.rs', 'pub fn routing_mode_description', 0, 'at', 'uitext', 4, 0],
  ['routingModeDescGlobal', 'gui/widgets.rs', 'pub fn routing_mode_description', 0, 'at', 'uitext', 5, 0],
  ['routingModeDescDirect', 'gui/widgets.rs', 'pub fn routing_mode_description', 0, 'at', 'uitext', 6, 0],
  ['routingModeDetailRule', 'gui/widgets.rs', 'pub fn routing_mode_detail', 0, 'at', 'uitext', 2, 0],
  ['routingModeDetailChinaDirect', 'gui/widgets.rs', 'pub fn routing_mode_detail', 0, 'at', 'uitext', 4, 0],
  ['routingModeDetailGfwList', 'gui/widgets.rs', 'pub fn routing_mode_detail', 0, 'at', 'uitext', 6, 0],
  ['routingModeDetailGlobal', 'gui/widgets.rs', 'pub fn routing_mode_detail', 0, 'at', 'uitext', 7, 0],
  ['routingModeDetailDirect', 'gui/widgets.rs', 'pub fn routing_mode_detail', 0, 'at', 'uitext', 8, 0],
  ['ruleActionDirect', 'gui/widgets.rs', 'pub fn rule_action_label', 0, 'at', 'uitext', 2, 0],
  ['ruleActionProxy', 'gui/widgets.rs', 'pub fn rule_action_label', 0, 'at', 'uitext', 3, 0],
  ['ruleActionReject', 'gui/widgets.rs', 'pub fn rule_action_label', 0, 'at', 'uitext', 4, 0],
  ['matcherLabelDomain', 'gui/widgets.rs', 'RuleMatcher::Domain(value) =>', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['matcherLabelDomainSuffix', 'gui/widgets.rs', 'RuleMatcher::DomainSuffix(value) =>', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['matcherLabelGeoSite', 'gui/widgets.rs', 'RuleMatcher::GeoSite(value) =>', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['matcherLabelGeoIp', 'gui/widgets.rs', 'RuleMatcher::GeoIp(value) =>', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['matcherLabelIpCidr', 'gui/widgets.rs', 'RuleMatcher::IpCidr(value) =>', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['matcherLabelDestinationPort', 'gui/widgets.rs', 'RuleMatcher::DestinationPort(value) =>', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['matcherLabelProcessName', 'gui/widgets.rs', 'RuleMatcher::ProcessName(value) =>', 0, 'nonAscii', 'uitext', 0, 0, true],
  ['matcherLabelNetworkTcp', 'gui/widgets.rs', 'NetworkProtocol::Tcp) =>', 0, 'nonAscii', 'uitext'],
  ['matcherLabelNetworkUdp', 'gui/widgets.rs', 'NetworkProtocol::Udp) =>', 0, 'nonAscii', 'uitext'],
  ['matcherLabelQuic', 'gui/widgets.rs', 'RuleMatcher::Quic =>', 0, 'nonAscii', 'uitext'],
  ['ruleDraftDomain', 'gui/types.rs', 'pub fn label(self) -> ', 0, 'at', 'uitext', 2, 0],
  ['ruleDraftDomainSuffix', 'gui/types.rs', 'pub fn label(self) -> ', 0, 'at', 'uitext', 3, 0],



  ['ruleDraftDestinationPort', 'gui/types.rs', 'pub fn label(self) -> ', 0, 'at', 'uitext', 7, 0],
  ['ruleDraftProcessName', 'gui/types.rs', 'pub fn label(self) -> ', 0, 'at', 'uitext', 8, 0],



  ['ruleMoveUp', 'gui/views/proxy.rs', 'index > 0, egui::Button::new(', 0, 'at', 'uitext', 1, 0],
  ['ruleMoveDown', 'gui/views/proxy.rs', 'index + 1 < self.proxy_config.rules.len(),', 0, 'at', 'uitext', 3, 0],
  ['ruleRemove', 'gui/views/proxy.rs', 'if ui.button(', 4, 'at', 'uitext', 0, 1],
  ['busyProcessingConfig', 'gui/views/proxy.rs', 'if !editable {', 0, 'at', 'uitext', 1, 0],
  ['busyProcessingRules', 'gui/views/proxy.rs', 'if !editable {', 1, 'at', 'uitext', 1, 0],
  ['processNameNeedsTun', 'gui/views/proxy.rs', 'PROCESS-NAME ', 0, 'nonAscii', 'uitext'],
  ['ruleInvalidPrefix', 'gui/views/proxy.rs', 'match rule.to_mihomo_rule() {', 0, 'at', 'uitext', 5, 0, true],
  ['optimizeAlreadyBest', 'gui/views/proxy.rs', 'self.status = if removed == 0 {', 0, 'at', 'uitext', 1, 0],
  ['optimizeRemovedN', 'gui/views/proxy.rs', 'self.status = if removed == 0 {', 0, 'at', 'uitext', 3, 0, true],
  ['overrideRulesTitle', 'gui/views/proxy.rs', 'ui.set_width(360.0);', 0, 'at', 'uitext', 1, 0],
  ['overrideRulesBody', 'gui/views/proxy.rs', 'ui.set_width(360.0);', 0, 'at', 'uitext', 2, 0],
  ['confirmOverride', 'gui/views/proxy.rs', '} else if ui.add(primary_btn(', 0, 'nonAscii', 'uitext'],
  ['confirmCancel', 'gui/views/proxy.rs', 'ui.set_width(360.0);', 0, 'at', 'uitext', 5, 0],
  ['ruleMoveUpGlyph', 'gui/views/proxy.rs', 'index > 0, egui::Button::new(', 0, 'at', 'uitext', 0, 0],
  ['ruleMoveDownGlyph', 'gui/views/proxy.rs', 'index + 1 < self.proxy_config.rules.len(),', 0, 'at', 'uitext', 1, 0],
  ['ruleRemoveGlyph', 'gui/views/proxy.rs', 'if ui.button(', 4, 'at', 'uitext', 0, 0],
  // 六个预设按钮：标签行 +5 才是 on_hover_text，用 occurrence 点名第几个按钮。
  ['presetAiHover', 'gui/views/proxy.rs', '.add(icons::button_with_icon(', 0, 'at', 'uitext', 5, 0],
  ['presetMediaHover', 'gui/views/proxy.rs', '.add(icons::button_with_icon(', 1, 'at', 'uitext', 5, 0],
  ['presetSocialHover', 'gui/views/proxy.rs', '.add(icons::button_with_icon(', 2, 'at', 'uitext', 5, 0],
  ['presetDevHover', 'gui/views/proxy.rs', '.add(icons::button_with_icon(', 3, 'at', 'uitext', 5, 0],
  ['presetGameHover', 'gui/views/proxy.rs', '.add(icons::button_with_icon(', 4, 'at', 'uitext', 5, 0],
  ['presetAdHover', 'gui/views/proxy.rs', '.add(icons::button_with_icon(', 5, 'at', 'uitext', 5, 0],
  ['injectionLine', 'gui/views/proxy.rs', 'injected.join(', 0, 'at', 'uitext', -1, 0, true],
  ['injectionJoinSep', 'gui/views/proxy.rs', 'injected.join(', 0, 'at', 'uitext', 0, 0],
  // 今日规则命中卡（Rust views/proxy.rs:1084-1118）。
  ['hitsSummary', 'gui/views/proxy.rs', 'self.rule_hits_today.iter().map(|hit| hit.hits).sum()', 0, 'at', 'uitext', 11, 0, true],
  ['hitsArrow', 'gui/views/proxy.rs', 'ui.label(RichText::new(&hit.rule).small()', 0, 'at', 'uitext', 1, 0, true],
  ['hitsCount', 'gui/views/proxy.rs', 'hit.hits', 2, 'at', 'uitext', 0, 0, true],
  // ---- DNS / TUN 网络分栏（Rust views/proxy.rs:1553-1702）。
  ['netSectionHint', 'gui/views/proxy.rs', 'section_title(ui, "DNS / TUN"', 0, 'at', 'uitext', 0, 0],
  ['netModeStrong', 'gui/views/proxy.rs', 'ui.strong(', 0, 'at', 'uitext', 0, 0],
  ['netModeSystem', 'gui/views/proxy.rs', 'self.proxy_config.mode, ProxyMode::System,', 0, 'at', 'uitext', 0, 0],
  ['netKernelLabel', 'gui/views/proxy.rs', 'from_id_salt("proxy_kernel_preference")', 0, 'at', 'uitext', -1, 0],
  ['netKernelAuto', 'gui/views/proxy.rs', 'KernelPreference::Auto =>', 0, 'at', 'uitext', 0, 0],
  ['netPortMixed', 'gui/views/proxy.rs', 'DragValue::new(&mut self.proxy_config.mixed_port)', 0, 'at', 'uitext', -2, 0],
  ['netPortController', 'gui/views/proxy.rs', 'DragValue::new(&mut self.proxy_config.controller_port)', 0, 'at', 'uitext', -2, 0],
  ['netAllowLan', 'gui/views/proxy.rs', '&mut self.proxy_config.allow_lan,', 0, 'at', 'uitext', 0, 0],
  ['netTerminalProxy', 'gui/views/proxy.rs', '&mut self.proxy_config.terminal_proxy,', 0, 'at', 'uitext', 1, 0],
  ['netTerminalProxyHover', 'gui/views/proxy.rs', 'HKCU', 0, 'marker', 'uitext'],
  ['netTunStrong', 'gui/views/proxy.rs', 'ui.strong(', 1, 'at', 'uitext', 0, 0],
  ['netStackLabel', 'gui/views/proxy.rs', 'from_id_salt("tun_stack")', 0, 'at', 'uitext', -1, 0],
  ['netAutoRoute', 'gui/views/proxy.rs', '&mut self.proxy_config.tun.auto_route,', 0, 'at', 'uitext', 0, 0],
  ['netAutoDetect', 'gui/views/proxy.rs', 'self.proxy_config.tun.auto_detect_interface,', 0, 'at', 'uitext', 1, 0],
  ['netDnsEnable', 'gui/views/proxy.rs', '&mut self.proxy_config.dns.enabled,', 0, 'at', 'uitext', 0, 0],
  ['netBusyNotice', 'gui/views/proxy.rs', 'if !editable {', 2, 'at', 'uitext', 1, 0],
  ['netSaveButton', 'gui/views/proxy.rs', 'save_clicked = ui.add(primary_btn(', 1, 'at', 'uitext', 0, 0],
  // ---- 活跃连接分栏里缺的两条。
  ['connFilterLabel', 'gui/views/proxy.rs', 'desired_width(180.0)', 0, 'at', 'uitext', -4, 0],
  ['connEmptyIdle', 'gui/views/proxy.rs', 'RichText::new(if self.active_connections.is_empty() {', 0, 'at', 'uitext', 1, 0],
  ['connEmptyNoMatch', 'gui/views/proxy.rs', 'RichText::new(if self.active_connections.is_empty() {', 0, 'at', 'uitext', 3, 0],
  ['connEmptyStopped', 'gui/views/proxy.rs', 'proxy_status.state != ProxyState::Running', 0, 'at', 'uitext', 2, 0],
  ['connDisconnectHover', 'gui/views/proxy.rs', '.add(danger_btn(', 0, 'at', 'uitext', 1, 0],
  // ---- 运行日志分栏（Rust views/proxy.rs:1876-1895）。
  ['logsTitle', 'gui/views/proxy.rs', '.map(KernelKind::display_name)', 0, 'at', 'uitext', 2, 0],
  ['logsSubtitle', 'gui/views/proxy.rs', '.map(KernelKind::display_name)', 0, 'at', 'uitext', 2, 1, true],
  ['logsEmpty', 'gui/views/proxy.rs', '.stick_to_bottom(true)', 0, 'at', 'uitext', 3, 0],
  ['logsKernelFallback', 'gui/views/proxy.rs', '.map(KernelKind::display_name)', 0, 'at', 'uitext', 1, 0],
  ['kernelUpdateFailed', 'gui/mod.rs', '内核更新失败', 0, 'marker', 'uitext', 0, 0, true],
  ['configReadFailed', 'gui/mod.rs', '读取代理配置失败', 0, 'marker', 'uitext', 0, 0, true],
  ['geoUpdatedOk', 'gui/mod.rs', '数据库更新指令', 0, 'marker', 'uitext'],
  ['geoUpdateFailed', 'gui/mod.rs', '规则更新失败', 0, 'marker', 'uitext', 0, 0, true],
  ['geoOfflineBusy', 'gui/mod.rs', 'let running = matches!(self.proxy_status.state, ProxyState::Running);', 0, 'at', 'uitext', 7, 0],
  ['geoOfflineDone', 'gui/mod.rs', 'crate::geo::provision(&report, None, true)', 0, 'at', 'uitext', 2, 0, true],
  ['geoRequesting', 'gui/mod.rs', 'let _ = tx.send(BgMsg::GeoUpdated(result.map_err(|error| error.to_string())));', 0, 'at', 'uitext', 4, 0],
  ['geoBadStatus', 'gui/mod.rs', 'let detail = body.trim();', 0, 'at', 'uitext', 2, 0, true],
  ['geoKernelDownloadFailed', 'gui/mod.rs', 'let detail = body.trim();', 0, 'at', 'uitext', 4, 0, true],
  ['geoRequestFailed', 'gui/mod.rs', 'let _ = tx.send(BgMsg::GeoUpdated(res));', 0, 'at', 'uitext', -2, 0, true],
  // ---- 配置保存/内核检查/日志读取的状态条（Rust gui/mod.rs）。
  ['savingConfig', 'gui/mod.rs', 'self.proxy_busy = true;', 0, 'at', 'uitext', 1, 0],
  ['configSaved', 'gui/mod.rs', 'BgMsg::ProxyConfigSaved(result) => {', 0, 'at', 'uitext', 5, 0],
  ['configSaveFailed', 'gui/mod.rs', 'BgMsg::ProxyConfigSaved(result) => {', 0, 'at', 'uitext', 8, 0, true],
  ['kernelCheckDone', 'gui/mod.rs', 'self.kernel_releases = releases;', 0, 'at', 'uitext', -1, 0, true],
  ['middleDotJoin', 'gui/mod.rs', 'self.kernel_releases = releases;', 0, 'at', 'uitext', -2, 0],
  ['sessionTraffic', 'gui/views/proxy.rs', 'format_bytes(self.traffic_up_total),', 0, 'at', 'uitext', -1, 0, true],
  ['pagerInfo', 'gui/views/nodes.rs', '\u9875\u0020\u00b7\u0020\u5171', 0, 'marker', 'uitext', 0, 0, true],
  ['pagerPrev', 'gui/views/nodes.rs', '\u4e0a\u4e00\u9875', 0, 'marker', 'uitext', 0, 0, true],
  ['pagerNext', 'gui/views/nodes.rs', '\u4e0b\u4e00\u9875', 0, 'marker', 'uitext', 0, 0, true],
  ['logReadFailed', 'gui/mod.rs', 'BgMsg::ProxyLog(Ok(log)) => self.proxy_log = log,', 0, 'at', 'uitext', 2, 0, true],
  ['kernelNoLatest', 'gui/mod.rs', '未取到最新版本', 0, 'marker', 'uitext'],
  // 启动/切换节点的结果文案（Rust gui/mod.rs 的 switch_proxy_node 与 Degraded 分支）。
  // 这几条是"代理起来了但节点其实是通的/不通"要说实话的地方，之前 vcc 自己编过措辞。
  ['nodeDegradedMarked', 'gui/mod.rs', '\u5f53\u524d\u8282\u70b9\u8fde\u901a\u6027\u5f02\u5e38', 0, 'marker', 'uitext', 0, 0, true],
  ['proxyStartedWithNode', 'gui/mod.rs', '\u4ee3\u7406\u5df2\u542f\u52a8\uff0c\u5df2\u8fde\u63a5\u8282\u70b9', 0, 'marker', 'uitext', 0, 0, true],
  ['switchedSmoothly', 'gui/mod.rs', '\u5df2\u5e73\u6ed1\u5207\u6362\u81f3\u8282\u70b9', 0, 'marker', 'uitext', 0, 0, true],
  ['switchNodeFailed', 'gui/mod.rs', '\u5207\u6362\u8282\u70b9\u5931\u8d25', 0, 'marker', 'uitext', 0, 0, true],
  ['startingProxyWithNode', 'gui/mod.rs', '\u26a1 \u6b63\u5728\u6781\u901f\u5f02\u6b65\u542f\u52a8\u4ee3\u7406\u5e76\u8fde\u63a5', 0, 'marker', 'uitext', 0, 0, true],
  ['switchingToNode', 'gui/mod.rs', '\u26a1 \u6b63\u5728\u6781\u901f\u5f02\u6b65\u5207\u6362\u81f3', 0, 'marker', 'uitext', 0, 0, true],
  ['proxyStartedTun', 'gui/mod.rs', '\u4ee3\u7406\u5df2\u542f\u52a8\uff0cTUN', 0, 'marker', 'uitext'],
  ['proxyStartedSystem', 'gui/mod.rs', '\u4ee3\u7406\u5df2\u542f\u52a8\uff0c\u7cfb\u7edf\u4ee3\u7406\u5df2\u542f\u7528', 0, 'marker', 'uitext'],
  // ---- 节点池测速的状态栏文案（Rust gui/mod.rs 的四个入口 + SpeedDone，widgets.rs 的每拍进度）。
  //      之前 vcc 自己编了"订阅测速完成/正在测速当前订阅: x/y"，既不是原文，也没带阶段与耗时。
  ['speedGroupStatus', 'gui/mod.rs', 'group.source_name, group.total', 0, 'at', 'uitext', -1, 0, true],
  ['speedGroupEmpty', 'gui/mod.rs', 'if ids.is_empty() {', 1, 'at', 'uitext', 1, 0, false],
  ['speedAllInitial', 'gui/mod.rs', 'pub(crate) fn speedtest_all_sources(&mut self) {', 0, 'at', 'uitext', 5, 0, false],
  ['speedAllProgress', 'gui/mod.rs', 'BgMsg::SourceSpeedProgress { done, total } => {', 0, 'at', 'uitext', 1, 0, true],
  ['speedDoneSummary', 'gui/mod.rs', 'sum.ok, sum.total, avg, avg_dl', 0, 'at', 'uitext', -1, 0, true],
  ['speedDoneFailed', 'gui/mod.rs', 'BgMsg::SpeedDone(Err(e)) => {', 0, 'at', 'uitext', 2, 0, true],
  ['speedPageLabel', 'gui/mod.rs', 'self.speedtest_ids(Some(ids), &format!(', 0, 'at', 'uitext', 0, 0, true],
  ['speedPageEmpty', 'gui/mod.rs', 'pub(crate) fn speedtest_page(&mut self) {', 0, 'at', 'uitext', 3, 0, false],
  ['speedOneStatus', 'gui/mod.rs', 'pub(crate) fn speedtest_one(&mut self, id: String) {', 0, 'at', 'uitext', 4, 0, false],
  ['speedAddedSourceLabel', 'gui/mod.rs', 'self.speedtest_ids(', 0, 'at', 'uitext', 2, 0, true],
  ['phaseTcpPrefilter', 'gui/widgets.rs', 'SpeedTestPhase::TcpPrefilter =>', 0, 'at', 'uitext', 0, 0, false],
  ['phaseRealProxy', 'gui/widgets.rs', 'SpeedTestPhase::RealProxy =>', 0, 'at', 'uitext', 0, 0, false],
  ['speedProgressFormat', 'gui/widgets.rs', '{phase}{detail}', 0, 'at', 'uitext', 0, 0, true],
  ['speedBatchDetail', 'gui/widgets.rs', 'Some(kernel), Some(batch), Some(total)', 0, 'at', 'uitext', 1, 0, true],
  ['speedKernelDetail', 'gui/widgets.rs', '(Some(kernel), _, _) => format!(', 0, 'at', 'uitext', 0, 0, true],
  // ---- 控制台节点卡的右键菜单配套文案（Rust gui/views/proxy.rs 的 compact_node_card 区）。
  //      右键菜单本身四条早已在 UiText（menuDetails/Speedtest/IpRisk/LockEgress），缺的是
  //      两条 hover 提示与"自动选路中左键被忽略"那句状态 —— 没有它们界面就不会告诉他有菜单。
  ['cardHoverAutoRouting', 'gui/views/proxy.rs', '.on_hover_text(if auto_routing {', 0, 'at', 'uitext', 1, 0, false],
  ['cardHoverPlain', 'gui/views/proxy.rs', '.on_hover_text(if auto_routing {', 0, 'at', 'uitext', 3, 0, false],
  ['autoRoutingClickIgnored', 'gui/views/proxy.rs', 'if response.clicked() && auto_routing && !is_selected {', 0, 'at', 'uitext', 1, 0, false],
  ['menuDeleteNode', 'gui/views/nodes.rs', 'theme::danger())).clicked()', 0, 'at', 'uitext', 0, 0, false],
  ['egressLockedStatus2', 'gui/views/proxy.rs', 'if let Some((id, name)) = locked {', 0, 'at', 'uitext', 5, 0, true],
  ['speedGroupHover', 'gui/views/nodes.rs', '!self.busy && selected_group.is_some(),', 0, 'at', 'uitext', 3, 0, false],
  ['speedAllHover', 'gui/views/nodes.rs', '.add_enabled(!self.busy, egui::Button::new(', 0, 'at', 'uitext', 1, 0, false],
];

// 纯 ASCII 的常量（比如 NO_PROXY 列表）直接生成字符串字面量，可读性优先；
// 含中文的才走码点数组。

const hasNonAscii = (text) => /[^\x00-\x7f]/.test(text);

function literalsIn(line) {
  return [...line.matchAll(/r?"((?:[^"\\]|\\.)*)"/g)].map((m) => m[1]);
}

function pickLiteral(lines, anchor, occurrence, name, mode = 'nonAscii') {
  const accept = mode === 'any' ? () => true : hasNonAscii;
  const hits = [];
  lines.forEach((line, index) => {
    if (line.includes(anchor)) hits.push(index);
  });
  if (hits.length <= occurrence) {
    console.error(`${name}: anchor "${anchor}" matched ${hits.length} line(s), want #${occurrence}`);
    process.exit(1);
  }
  const center = hits[occurrence];
  for (let offset = 0; offset <= 3; offset += 1) {
    for (const index of offset === 0 ? [center] : [center - offset, center + offset]) {
      const line = lines[index];
      if (line === undefined) continue;
      const found = literalsIn(line).filter(accept);
      if (found.length === 1) return found[0];
      if (found.length > 1) {
        console.error(`${name}: ${found.length} matching literals near "${anchor}"`);
        process.exit(1);
      }
    }
  }
  console.error(`${name}: no non-ASCII literal near "${anchor}" (line ${center + 1})`);
  process.exit(1);
}

/// 精确偏移取字面量：锚点行往下/往上 offset 行，取该行第 want 个非 ASCII 字面量。
/// 用 +-3 找窗口会被相邻行的同类字面量污染（内核表头六行挨在一起，实测就串了），
/// 所以这里由偏移量点名，并把真正命中的行号记进报告（litLine）。
let litLine = -1;
function pickAt(lines, anchor, occurrence, name, offset, want) {
  const hits = [];
  lines.forEach((line, index) => {
    if (line.includes(anchor)) hits.push(index);
  });
  if (hits.length <= occurrence) {
    console.error(`${name}: anchor "${anchor}" matched ${hits.length} line(s), want #${occurrence}`);
    process.exit(1);
  }
  const index = hits[occurrence] + offset;
  const line = lines[index];
  if (line === undefined) {
    console.error(`${name}: line ${index + 1} out of file`);
    process.exit(1);
  }
  const found = literalsIn(line).filter(hasNonAscii);
  if (found.length <= want) {
    console.error(`${name}: ${found.length} non-ASCII literal(s) on line ${index + 1} (${JSON.stringify(line.trim())}), want #${want}`);
    process.exit(1);
  }
  litLine = index + 1;
  return found[want];
}

/// 跨行字面量：Rust 的行尾 `\` 续行会把下一条源的物理行接进同一个字符串，
/// 所以先把续行折平，再用"整条字面量必须唯一包含这段 ASCII 标记"点名它。
/// 转义按 Rust 语义还原：`\n` 是真空行、`\\` 是单斜杠、`\\"` 是引号。
function pickMarker(text, marker, name) {
  // 跨行字面量：Rust 用行尾反斜杠把下一条物理行接进同一个字符串，按行取必然取不到。
  // 先折平续行（连同下一行的缩进），再用"整条字面量唯一包含这段 ASCII 标记"点名它，
  // 最后按 Rust 语义还原转义（\n 是真空行，\\ 是单斜杠，\" 是引号）。
  const BS = String.fromCharCode(92);
  const NL = String.fromCharCode(10);
  const CR = String.fromCharCode(13);
  const TAB = String.fromCharCode(9);

  let flat = '';
  for (let i = 0; i < text.length; i += 1) {
    const ch = text[i];
    if (ch === BS && (text[i + 1] === NL || (text[i + 1] === CR && text[i + 2] === NL))) {
      i += text[i + 1] === CR ? 3 : 2;
      while (text[i] === ' ' || text[i] === TAB) {
        i += 1;
      }
      i -= 1;
      continue;
    }
    flat += ch;
  }

  const hits = [...flat.matchAll(/"((?:[^"\\]|\\.)*)"/g)]
    .map((m) => m[1])
    .filter((lit) => lit.includes(marker));
  if (hits.length !== 1) {
    console.error(`${name}: marker "${marker}" matched ${hits.length} literal(s), want exactly 1`);
    process.exit(1);
  }
  const rawLine = text.split(/\r?\n/).findIndex((line) => line.includes(marker));
  litLine = rawLine + 1;

  const raw = hits[0];
  let out = '';
  for (let i = 0; i < raw.length; i += 1) {
    if (raw[i] !== BS) {
      out += raw[i];
      continue;
    }
    const next = raw[i + 1];
    if (next === 'n') {
      out += NL;
    } else if (next === 't') {
      out += TAB;
    } else if (next !== undefined) {
      out += next;
    }
    i += 1;
  }
  return out;
}

function toCppArray(text) {
  // 一律用十六进制码点：混着 "x" 字面量会让 initializer_list<char32_t> 推不出公共类型。
  return [...text].map((ch) => '0x' + ch.codePointAt(0).toString(16).toUpperCase().padStart(4, '0'));
}

const buckets = new Map();
for (const target of Object.keys(outputs)) {
  buckets.set(target, { functions: [], declarations: [] });
}

for (const [name, file, anchor, occurrence, mode, target = 'sysproxy', offset = 0, want = 0, braces = false] of entries) {
  const lines = fs.readFileSync(RUST + '/' + file, 'utf8').split(/\r?\n/);
  litLine = -1;
  const literal = mode === 'at'
    ? pickAt(lines, anchor, occurrence, name, offset, want)
    : mode === 'marker'
      ? pickMarker(fs.readFileSync(RUST + '/' + file, 'utf8'), anchor, name)
      : pickLiteral(lines, anchor, occurrence, name, mode);
  // Rust 用 {} 占位，Qt 的 arg 要 %1/%2/...；分隔符（·）照样从 Rust 抠，
  // 这样带非 ASCII 的模板也不用我手打。
  // Rust 的行内捕获 `{value}` 和 `{}` 一样是占位符，不转换就会把花括号打进界面。
  let templateIndex = 0;
  // braces 是**逐条**的：调用方用 .arg() 才转 %n，自己做 .replace("{}", …) 的条目必须
  // 原样保留花括号（geo/sysproxy 那批就是）。自动判定过一次，三条测试套件当场红。
  const shaped = braces
    ? literal.replace(/\{\}|(?:\{[A-Za-z_][A-Za-z0-9_:.,#]*\})|(?:\{[:.,#][A-Za-z0-9_:.,#]*\})/g, () => `%${++templateIndex}`)
    : literal;
  const parts = toCppArray(shaped);
  const skeleton = [...shaped].filter((c) => c.codePointAt(0) < 0x7f).join('');
  const body = [];
  body.push(`// ${name} <- ${file}:${litLine}${braces ? ' ({} -> %n)' : ''}`);
  body.push(`// ASCII 骨架: ${JSON.stringify(skeleton)}`);
  body.push('QString ' + name + '()');
  body.push('{');
  if (hasNonAscii(literal)) {
    body.push('    return fromCodePoints({');
    for (let i = 0; i < parts.length; i += 8) {
      body.push('        ' + parts.slice(i, i + 8).join(', ') + ',');
    }
    body.push('    });');
  } else {
    // 纯 ASCII：直接写字面量，NO_PROXY 那种长列表还要能被人读懂。
    const escaped = literal.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
    body.push(`    return QStringLiteral("${escaped}");`);
  }
  body.push('}');
  body.push('');
  const bucket = buckets.get(target);
  if (!bucket) {
    console.error('unknown output target: ' + target + ' for ' + name);
    process.exit(1);
  }
  bucket.functions.push(body.join('\n'));
  bucket.declarations.push('QString ' + name + '();');
  console.log(`${target}.${name}: ${parts.length} chars, skeleton = ${JSON.stringify(skeleton)}`);
}

for (const [target, config] of Object.entries(outputs)) {
  const bucket = buckets.get(target);
  if (!bucket.functions.length) {
    continue;
  }
  const source = [
    '// 由 tools/gen-cjk-codepoints.mjs 从 Rust 源生成，请勿手改。',
    '// 这些提示文本要与 vpn-parser 的 Rust 实现逐字一致（错误消息会进界面和日志）。',
    config.include,
    '',
    '#include <initializer_list>',
    '',
    `namespace ${config.ns} {`,
    'namespace {',
    'QString fromCodePoints(std::initializer_list<char32_t> points)',
    '{',
    '    return QString::fromUcs4(points.begin(), qsizetype(points.size()));',
    '}',
    '',
    '}',
    '',
    ...bucket.functions,
    '}',
    '',
  ].join('\n');
  const header = [
    '// 由 tools/gen-cjk-codepoints.mjs 生成，请勿手改。',
    '#pragma once',
    '',
    '#include <QString>',
    '',
    `namespace ${config.ns} {`,
    '',
    ...bucket.declarations,
    '',
    '}',
    '',
  ].join('\n');
  fs.writeFileSync(config.source, source);
  fs.writeFileSync(config.header, header);
  console.log(target + ': wrote ' + bucket.functions.length + ' message(s)');
}
