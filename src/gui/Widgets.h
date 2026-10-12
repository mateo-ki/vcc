#pragma once

#include <QLabel>
#include <QString>

#include "proxy/MihomoConfig.h"
#include "models/Types.h"
#include "speedtest/SpeedtestTypes.h"

#include <cstdint>

QT_BEGIN_NAMESPACE
class QComboBox;
class QWidget;
QT_END_NAMESPACE

namespace Gui {

/// 单行、按当前宽度截断成 "…"（对应 Rust 的 `egui::Label::truncate()`）。
/// 卡片高度必须只由"有几行"决定：名字换行的那张卡会把整行拉高，于是同一个网格里
/// 有的卡 64px、有的 90px —— 他 2026-10-12 报的"卡片大小不一样"就是这个。
/// 完整文字挂成 tooltip，截断后仍然看得到全名（Rust 也是这么处理的）。
class ElidedLabel : public QLabel {
    Q_OBJECT

public:
    explicit ElidedLabel(QWidget *parent = nullptr);
    void setFullText(const QString &text);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void relayout();
    QString fullText_;
};

/// TUN 模式要创建虚拟网卡，非提权进程做不到；系统代理模式不需要。
/// 对应 Rust `gui::widgets::requires_tun_elevation`（纯函数，三条分支都有测试钉着）。
bool requiresTunElevation(Proxy::ProxyMode mode, bool elevated);

/// RFC3339 文本里的 HH:MM。Rust `widgets.rs::clock_of` 就是直接截 'T' 之后的 5 个字符
/// （库里存的是 UTC 文本），截不出来就原样返回 —— 状态卡与健康条共用这一份。
QString clockOf(const QString &at);

/// 对应 Rust `gui/widgets.rs:726 format_speedtest_progress`：测速每一拍的状态栏文案。
/// 阶段名 + 内核/批次 + 完成数 + 百分比 + 成功/失败 + 已耗时，全部来自 Rust 原文模板。
QString formatSpeedtestProgress(const Speedtest::SpeedTestProgress &progress);

/// 对应 Rust `gui/mod.rs` 的 `BgMsg::SpeedDone(Ok(sum))` 那三行：平均延迟取整带 ms，
/// 平均下载按 KB/s 留一位小数，缺值时两者都是 "-"。
QString speedDoneText(const Models::SpeedTestSummary &summary);

/// 对应 Rust `gui::widgets::format_bytes`：GB / MB / KB 三档除完之后按位数收尾。
QString formatBytes(quint64 bytes);
QString formatSpeed(quint64 bytesPerSecond);

/// 全软件唯一的下拉框外观：控件自带样式表（不是只靠应用级 QSS —— 实测埋在
/// Card/InnerBlock 里的那些下拉框根本不铺背景，只有自带的这份一定生效；
/// 离屏试过给应用级加更高特异度的规则，四条候选写法量下来都没用）。
/// 颜色一律写语义槽，由 Theme::setWidgetCss 填色并在切主题时重挂，所以它跟着主题走。
/// 注意成本：这份 CSS 每造一颗要多花 ~4.8 ms 解析，所以"每行一颗常驻下拉框"这种
/// 结构要避免 —— 规则表的动作列就是为此改成委托编辑器的（一次只存在一颗）。
void decorateComboBox(QComboBox *combo);
/// 造一个已经装饰好的下拉框：所有新建下拉框都走这里，样式才有统一可言。
QComboBox *makeComboBox(QWidget *parent);
/// 递归给子树里每个下拉框重刷样式（主题切换、动态建行时用）。
void decorateComboBoxesIn(QWidget *root);
/// 一整棵子树里的输入框/旋钮/勾选框/单选钮/下拉框统一外观（同一个道理：每个控件自带
/// 一份语义槽模板，应用级 QSS 在卡片里铺不进去）。页面建完控件后调一次。
void decorateFieldsIn(QWidget *root);

}
