#include "gui/NodeCard.h"

#include "gui/Widgets.h"

#include "gui/CountryNames.h"
#include "gui/UiText.h"
#include "gui/NodeText.h"
#include "gui/Theme.h"

#include <QAction>
#include <QFont>
#include <QMenu>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QPainter>
#include <QPalette>
#include <QVBoxLayout>
#include <QSpacerItem>

namespace Gui {

namespace {

QColor rgb(uint value)
{
    return QColor((value >> 16) & 0xff, (value >> 8) & 0xff, value & 0xff);
}

constexpr int kCardRadius = 12;
constexpr int kMarginH = 14;
constexpr int kMarginV = 11;
constexpr int kLineGap = 4;
constexpr int kColumnGap = 8;
constexpr int kNamePx = 14;
constexpr int kDetailPx = 12;

/// Palette 槽名 -> 颜色（卡片文字在 paintEvent 里画，所以取色也在这里）。
QColor slotColor(const Theme::Palette &pal, const QString &slot)
{
    if (slot == QLatin1String("success")) {
        return pal.success;
    }
    if (slot == QLatin1String("warning")) {
        return pal.warning;
    }
    if (slot == QLatin1String("danger")) {
        return pal.danger;
    }
    if (slot == QLatin1String("textSecondary")) {
        return pal.textSecondary;
    }
    if (slot == QLatin1String("muted")) {
        return pal.muted;
    }
    if (slot == QLatin1String("accentStrong")) {
        return pal.accentStrong;
    }
    return pal.textPrimary;
}
constexpr int kBadgeRadius = 4;
/// 国家那段至少要有这点宽度才值得画：再窄就整段不画，别挤成一截看不懂的残字。
constexpr int kMinCountryPx = 24;

/// 延迟文案的角色名（NodeText 的 latencyText 给的是 QSS objectName）落到 Palette 槽名。
/// 也顺手把"控制台的延迟分档色和节点池不是一套"结束（旧控制台内联了 <150/<350 三档，
/// 节点池是 Rust 的 <200/<800）。
QString latencySlot(const QString &role)
{
    if (role == QLatin1String("Success")) {
        return QStringLiteral("success");
    }
    if (role == QLatin1String("Danger")) {
        return QStringLiteral("danger");
    }
    if (role == QLatin1String("Warning")) {
        return QStringLiteral("warning");
    }
    if (role == QLatin1String("Subtitle")) {
        return QStringLiteral("textSecondary");
    }
    return QStringLiteral("textPrimary");
}

}

NodeCardFrame::NodeCardFrame(QWidget *parent) : QFrame(parent)
{
    // 竖向不伸：一行只有一张卡时，QGridLayout 会把多余的行高全给它，
    // 卡片就被拉成整屏高（"只有一个节点，卡片特别大"）。
    // 横向 Expanding + 最小宽 0：列宽由网格等分决定，不让某一张卡的长名字把那一列撑宽。
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumWidth(0);

    // 名字换行的那张卡会把整行拉高，于是同一网格里卡高不齐；64 是节点池原来那张卡的
    // 高度（他 2026-10-12 选"统一到节点池那种"），Rust 那边是 min-height 58。
    setMinimumHeight(64);

    // 结构与 Rust 的 compact_node_card 一致：第一行名字单行截断，第二行
    // 协议 · 国家 · 右对齐延迟。**四段文字都在 paintEvent 里画，卡片不养子控件**：
    // 用 QLabel 的话，应用级那条 `QWidget { background: pageBg; color: textPrimary }`
    // （Theme.cpp:305）会在 polish 时把显式调色板整个盖掉 —— 实测四段文字全变成
    // textPrimary，标签还各自糊了一块 pageBg 底色在卡的自绘底上（他 2026-10-12 报的
    // "字体背景没统一"）。子控件还要每卡 4 份样式模板，正是这张卡当初要避免的东西。
    auto *box = new QVBoxLayout(this);
    box->setContentsMargins(kMarginH, kMarginV, kMarginH, kMarginV);
    box->setSpacing(kLineGap);
    box->addItem(new QSpacerItem(0, 0));           // 只用来撑 sizeHint，没有子控件
    setLayout(box);
}

NodeCardMenu addNodeCardMenu(QMenu *menu, bool speedtestFirst, bool speedtestEnabled,
                             bool withDelete, bool withLockEgress)
{
    NodeCardMenu actions;
    auto appendSpeedtest = [&]() {
        actions.speedtest = menu->addAction(UiText::menuSpeedtest());
        actions.speedtest->setEnabled(speedtestEnabled);
    };
    auto appendDetails = [&]() { actions.details = menu->addAction(UiText::menuDetails()); };
    auto appendIpRisk = [&]() { actions.ipRisk = menu->addAction(UiText::menuIpRisk()); };

    if (speedtestFirst) {
        appendSpeedtest();
        appendDetails();
    } else {
        appendDetails();
        appendSpeedtest();
    }
    appendIpRisk();
    if (withDelete) {
        menu->addSeparator();
        actions.remove = menu->addAction(UiText::menuDeleteNode());
        // Rust 那条是 RichText::new("删除节点").color(theme::danger())，但 Qt 6 删掉了
        // QAction::setForeground，要给单条菜单项上色得给 QMenu 挂一个 item delegate。
        // 现在先留纯文字（功能一致，少一层颜色来源）。
    }
    if (withLockEgress) {
        menu->addSeparator();
        actions.lockEgress = menu->addAction(UiText::menuLockEgress());
    }
    return actions;
}

void NodeCardFrame::bind(const ProxyViewRow &row, const Proxy::ProxyRuntimeStatus &status,
                         const QString &selectedNodeId)
{
    const bool isRunning = status.localProxyActive() && status.nodeId == row.node.id;
    const bool isSelected = (selectedNodeId == row.node.id) || isRunning;

    nodeId_ = row.node.id;
    nodeName_ = row.node.name;
    setProperty("nodeId", nodeId_);
    setProperty("nodeName", nodeName_);

    nameText_ = row.node.name;
    protocolText_ = row.node.protocol.toUpper();
    countryText_.clear();
    if (!row.country.isNull() && !row.country.isEmpty()) {
        const CountryDisplay display = countryFlagAndName(row.country);
        countryText_ = QStringLiteral("%1 %2").arg(display.flag, display.name);
    }
    // 延迟文案与分档走 NodeText::latencyText —— 两张页面从此同一套（含"未测/失败"的说法）。
    const LatencyDisplay latency = latencyText(row.node);
    latencyText_ = latency.text;
    latencySlot_ = latencySlot(latency.role);

    setState(isRunning ? NodeCardState::Running
                       : (isSelected ? NodeCardState::Selected : NodeCardState::Normal));
    updateGeometry();
    update();
}

QSize NodeCardFrame::sizeHint() const
{
    const QFontMetrics nameMetrics(cardFont(this, kNamePx, true));
    const QFontMetrics detailMetrics(cardFont(this, kDetailPx, false));
    return QSize(160, kMarginV * 2 + nameMetrics.height() + kLineGap + detailMetrics.height());
}

NodeCardDetailRow NodeCardFrame::layoutDetailRow(const QRect &row, const QFontMetrics &detailMetrics,
                                                 const QFontMetrics &latencyMetrics,
                                                 const QString &protocol, const QString &country,
                                                 const QString &latency)
{
    // 延迟先占住右边（它是唯一不许截断的一段），剩下的高度空间给协议+国家。
    const int latencyWidth = latencyMetrics.horizontalAdvance(latency);
    NodeCardDetailRow out;
    out.latency = QRect(row.right() - latencyWidth + 1, row.y(), latencyWidth, row.height());
    const int middleRight = out.latency.left() - kColumnGap;
    out.protocol = QRect(row.left(), row.y(), qMax(0, middleRight - row.left()), row.height());
    const int countryLeft = row.left() + detailMetrics.horizontalAdvance(protocol) + kColumnGap;
    out.country = country.isEmpty() || middleRight - countryLeft < kMinCountryPx
        ? QRect()
        : QRect(countryLeft, row.y(), middleRight - countryLeft, row.height());
    return out;
}

void NodeCardFrame::setState(NodeCardState state)
{
    if (state == state_) {
        return;
    }
    state_ = state;
    update();
}

NodeCardFrame::Skin NodeCardFrame::skinForState() const
{
    // 一次取色：paintEvent 会为每张卡各调用这里，Theme::current() 是带锁的调色板拷贝。
    const Theme::Palette pal = Theme::current();
    switch (state_) {
    case NodeCardState::Running:
        return {pal.successBg, pal.successBg, pal.success, pal.success, 2};
    case NodeCardState::Selected:
        return {pal.accentBg, pal.accentBg, pal.accent, pal.accentStrong, 2};
    case NodeCardState::Normal:
        return {pal.panel, pal.panelHover, pal.borderSubtle, pal.muted, 1};
    }
    return {pal.panel, pal.panelHover, pal.borderSubtle, pal.muted, 1};
}

void NodeCardFrame::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);

    const Skin skin = skinForState();
    const bool hovered = underMouse();
    const qreal pen = skin.borderWidth;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(hovered ? skin.borderHover : skin.border, pen));
    painter.setBrush(hovered ? skin.fillHover : skin.fill);
    // 描边画在控件矩形内侧（和 QSS 的盒模型一致），所以四边各收半个笔宽。
    painter.drawRoundedRect(QRectF(QPointF(pen / 2.0, pen / 2.0),
                                   QPointF(width() - pen / 2.0, height() - pen / 2.0)),
                            kCardRadius, kCardRadius);

    const Theme::Palette pal = Theme::current();
    const int textWidth = qMax(0, width() - 2 * kMarginH);
    int y = kMarginV;

    const QFont nameFont = cardFont(this, kNamePx, true);
    painter.setFont(nameFont);
    painter.setPen(state_ == NodeCardState::Normal ? pal.textPrimary : pal.accentStrong);
    const QFontMetrics nameMetrics(nameFont);
    painter.drawText(QRect(kMarginH, y, textWidth, nameMetrics.height()),
                     Qt::AlignLeft | Qt::AlignVCenter | Qt::TextShowMnemonic,
                     nameMetrics.elidedText(nameText_, Qt::ElideRight, textWidth));
    y += nameMetrics.height() + kLineGap;

    const QFont detailFont = cardFont(this, kDetailPx, false);
    const QFontMetrics detailMetrics(detailFont);
    const QFont latencyFont = cardFont(this, kDetailPx, true);
    const QFontMetrics latencyMetrics(latencyFont);
    const QRect detailRow(kMarginH, y, textWidth, detailMetrics.height());
    lastDetailRow_ = layoutDetailRow(detailRow, detailMetrics, latencyMetrics, protocolText_,
                                     countryText_, latencyText_);

    painter.setFont(detailFont);
    painter.setPen(pal.muted);
    painter.drawText(lastDetailRow_.protocol, Qt::AlignLeft | Qt::AlignVCenter,
                     detailMetrics.elidedText(protocolText_, Qt::ElideRight,
                                              lastDetailRow_.protocol.width()));
    if (lastDetailRow_.country.width() > 0) {
        painter.setPen(pal.textSecondary);
        painter.drawText(lastDetailRow_.country, Qt::AlignLeft | Qt::AlignVCenter,
                         detailMetrics.elidedText(countryText_, Qt::ElideRight,
                                                  lastDetailRow_.country.width()));
    }
    // 延迟右对齐；分档色走 Palette 槽名（success/warning/danger/textSecondary）。
    painter.setFont(latencyFont);
    painter.setPen(slotColor(pal, latencySlot_));
    painter.drawText(lastDetailRow_.latency, Qt::AlignLeft | Qt::AlignVCenter, latencyText_);
}

void NodeCardFrame::enterEvent(QEnterEvent *event)
{
    QFrame::enterEvent(event);
    update();
}

void NodeCardFrame::leaveEvent(QEvent *event)
{
    QFrame::leaveEvent(event);
    update();
}

NodeBadge::NodeBadge(const QString &text, QWidget *parent) : QLabel(text, parent)
{
}

void NodeBadge::setTone(Tone tone)
{
    tone_ = tone;
    // 配色全部取当前主题：这套徽章以前也写死过 #dcfce7/#15803d/#dbeafe/#1d4ed8，
    // 切主题时不会跟着走（Rust 侧同一颗徽章用的是 theme::success_bg()/accent_bg()）。
    const Theme::Palette pal = Theme::current();
    QColor background = pal.panelHover;
    QColor foreground = pal.textSecondary;
    int pixelSize = 10;
    int sidePadding = 11;
    switch (tone) {
    case Tone::Protocol:
        break;
    case Tone::Running:
        background = pal.successBg;
        foreground = pal.success;
        pixelSize = 11;
        sidePadding = 14;
        break;
    case Tone::Selected:
        background = pal.accentBg;
        foreground = pal.accentStrong;
        pixelSize = 11;
        sidePadding = 14;
        break;
    }

    QFont font = this->font();
    font.setPixelSize(pixelSize);
    font.setWeight(QFont::Bold);
    setFont(font);

    QPalette palette = this->palette();
    palette.setColor(QPalette::WindowText, foreground);
    setPalette(palette);

    background_ = background;
    // QSS 的 padding: 2px 8px —— 文本要留出这段内边距，圆角底色要铺满整块。
    sidePadding_ = sidePadding;
    setContentsMargins(sidePadding, kVerticalPadding, sidePadding, kVerticalPadding);
    updateGeometry();
    update();
}

QSize NodeBadge::sizeHint() const
{
    // contentsMargins 已经进了 QLabel::sizeHint，再加一次会把徽章撑大一圈。
    return QLabel::sizeHint();
}

QSize NodeBadge::minimumSizeHint() const
{
    return QLabel::minimumSizeHint().expandedTo(QSize(0, QLabel::sizeHint().height()));
}

void NodeBadge::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background_);
    painter.drawRoundedRect(rect(), kBadgeRadius, kBadgeRadius);
    QLabel::paintEvent(event);
}

QFont cardFont(const QWidget *widget, int pixelSize, bool bold)
{
    QFont font = widget->font();
    font.setPixelSize(pixelSize);
    font.setWeight(bold ? QFont::Bold : QFont::Normal);
    return font;
}

}
