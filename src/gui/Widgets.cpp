#include "gui/Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QRadioButton>
#include <QSpinBox>

#include <QFontMetrics>
#include <QResizeEvent>

#include "gui/Theme.h"
#include "gui/UiText.h"
#include "kernel/KernelTypes.h"

namespace Gui {

ElidedLabel::ElidedLabel(QWidget *parent) : QLabel(parent)
{
    setWordWrap(false);
}

void ElidedLabel::setFullText(const QString &text)
{
    fullText_ = text;
    setToolTip(text);
    relayout();
}

void ElidedLabel::resizeEvent(QResizeEvent *event)
{
    QLabel::resizeEvent(event);
    relayout();
}

void ElidedLabel::relayout()
{
    const QFontMetrics metrics(font());
    QLabel::setText(metrics.horizontalAdvance(fullText_) > width()
        ? metrics.elidedText(fullText_, Qt::ElideRight, width())
        : fullText_);
}

bool requiresTunElevation(Proxy::ProxyMode mode, bool elevated)
{
    return mode == Proxy::ProxyMode::Tun && !elevated;
}

QString speedDoneText(const Models::SpeedTestSummary &summary)
{
    const QString avg = summary.avgMs.has_value()
        ? QStringLiteral("%1ms").arg(QString::number(*summary.avgMs, 'f', 0))
        : QStringLiteral("-");
    const QString avgDownload = summary.avgDownloadKbps.has_value()
        ? QStringLiteral("%1KB/s").arg(QString::number(*summary.avgDownloadKbps, 'f', 1))
        : QStringLiteral("-");
    return UiText::speedDoneSummary()
        .arg(summary.ok)
        .arg(summary.total)
        .arg(avg, avgDownload);
}

QString formatSpeedtestProgress(const Speedtest::SpeedTestProgress &progress)
{
    const QString phase = progress.phase == Speedtest::SpeedTestPhase::TcpPrefilter
        ? UiText::phaseTcpPrefilter()
        : UiText::phaseRealProxy();
    const double percent = progress.total == 0
        ? 100.0
        : double(progress.done) * 100.0 / double(progress.total);

    QString detail;
    if (progress.kernel.has_value()) {
        detail = UiText::speedKernelDetail().arg(Kernel::displayName(*progress.kernel));
        if (progress.batchIndex.has_value() && progress.batchTotal.has_value()) {
            detail = UiText::speedBatchDetail()
                         .arg(Kernel::displayName(*progress.kernel))
                         .arg(qint64(*progress.batchIndex))
                         .arg(qint64(*progress.batchTotal));
        }
    }
    return UiText::speedProgressFormat()
        .arg(phase, detail)
        .arg(qint64(progress.done))
        .arg(qint64(progress.total))
        .arg(QString::number(percent, 'f', 1))
        .arg(qint64(progress.ok))
        .arg(qint64(progress.fail))
        .arg(QString::number(double(progress.elapsedMs) / 1000.0, 'f', 1));
}

QString clockOf(const QString &at)
{
    const int t = at.indexOf(QLatin1Char('T'));
    const QString time = t >= 0 ? at.mid(t + 1) : at;
    if (time.size() >= 5 && time.at(2) == QLatin1Char(':') && time.at(0).isDigit()) {
        return time.left(5);
    }
    return at;
}


QString formatBytes(quint64 bytes)
{
    const double value = static_cast<double>(bytes);
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        return QString::number(value / (1024.0 * 1024.0 * 1024.0), 'f', 2)
            + QStringLiteral(" GB");
    }
    if (bytes >= 1024ULL * 1024ULL) {
        return QString::number(value / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB");
    }
    if (bytes >= 1024ULL) {
        return QString::number(value / 1024.0, 'f', 1) + QStringLiteral(" KB");
    }
    return QString::number(value) + QStringLiteral(" B");
}

QString formatSpeed(quint64 bytesPerSecond)
{
    // 对应 Rust `widgets.rs::format_speed`：只有 MB/s（两位小数）、KB/s（一位）、B/s 三档，
    // 没有 GB 档 —— 和 format_bytes 不一样，别顺手复用。
    if (bytesPerSecond >= 1024 * 1024) {
        return QStringLiteral("%1 MB/s")
            .arg(bytesPerSecond / (1024.0 * 1024.0), 0, 'f', 2);
    }
    if (bytesPerSecond >= 1024) {
        return QStringLiteral("%1 KB/s").arg(bytesPerSecond / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 B/s").arg(bytesPerSecond);
}

void decorateComboBox(QComboBox *combo)
{
    if (combo == nullptr) {
        return;
    }
    // 与应用级 QSS 里那段 QComboBox 同值：底色=面板、1px 边框、圆角 6、悬停/聚焦换强调色
    // 描边；弹出列表也自带一份，免得弹层是系统白底。颜色全走语义槽，切主题时由
    // Theme::apply 按模板重挂（见 Theme::setWidgetCss）。
    Theme::setWidgetCss(combo, QStringLiteral(
        "QComboBox { background-color: {panel}; color: {textPrimary}; border: 1px solid {border};"
        " border-radius: 6px; padding: 4px 28px 4px 10px; font-size: 13px; min-height: 22px; }"
        "QComboBox:hover, QComboBox:focus { border: 1px solid {accent}; }"
        // 禁用态必须照样有边框：没有 :disabled 这一支的话，Qt 直接退回原生画法，
        // 表现就是"代理在跑时 DNS/TUN 那几个下拉框像没样式的灰块"。
        "QComboBox:disabled { background-color: {panel}; color: {muted}; border: 1px solid {border}; }"
        "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right;"
        " width: 22px; border-left: 1px solid {border}; border-top-right-radius: 6px;"
        " border-bottom-right-radius: 6px; background-color: {panelHover}; }"
        // 箭头必须走 `image:`：Qt 的 QSS 不认浏览器那套 "width:0;height:0 + border 拼三角形"，
        // 而 `image: none` 会把原生箭头关掉 —— 前者画不出东西、后者只剩一个空的 drop-down 区
        // （先是黑方块，去掉规则后又变成"没有箭头"，两轮都是这么来的）。
        // {arrowDown} 由 Theme::cssWithPalette 换成按当前调色板渲好的 PNG 路径。
        "QComboBox::down-arrow { image: url({arrowDown}); width: 10px; height: 10px; }"
        "QComboBox::drop-down:on, QComboBox::drop-down:pressed { background-color: {accentBg}; }"
        "QComboBox::drop-down:disabled { background-color: {panelHover};"
        " border-left: 1px solid {border}; }"
        "QComboBox QAbstractItemView { background-color: {panel}; color: {textPrimary};"
        " border: 1px solid {border}; selection-background-color: {accentBg};"
        " selection-color: {accentStrong}; padding: 4px; }"));
}

QComboBox *makeComboBox(QWidget *parent)
{
    auto *combo = new QComboBox(parent);
    decorateComboBox(combo);
    return combo;
}

void decorateComboBoxesIn(QWidget *root)
{
    if (root == nullptr) {
        return;
    }
    const QList<QComboBox *> combos = root->findChildren<QComboBox *>();
    for (QComboBox *combo : combos) {
        decorateComboBox(combo);
    }
}

namespace {

/// 输入框/旋钮/勾选框/单选钮：和下拉框同一个道理 —— 应用级 QSS 铺不进滚动区里的卡片，
/// 所以每个控件自带一份（颜色走语义槽，切主题由 Theme::apply 按模板重挂）。
void decorateField(QWidget *widget, const QString &pattern)
{
    if (widget == nullptr) {
        return;
    }
    widget->setAttribute(Qt::WA_StyledBackground, true);
    Theme::setWidgetCss(widget, pattern);
}

constexpr const char *kLineEditCss =
    "QLineEdit { background-color: {panel}; color: {textPrimary};"
    " border: 1px solid {border}; border-radius: 6px; padding: 4px 8px;"
    " font-size: 13px; min-height: 22px; }"
    "QLineEdit:hover, QLineEdit:focus { border: 1px solid {accent}; }"
    "QLineEdit:disabled { background-color: {panel}; color: {muted}; border: 1px solid {border}; }";

constexpr const char *kSpinBoxCss =
    "QSpinBox { background-color: {panel}; color: {textPrimary}; border: 1px solid {border};"
    " border-radius: 6px; padding: 4px 8px; font-size: 13px; min-height: 22px; }"
    "QSpinBox:hover, QSpinBox:focus { border: 1px solid {accent}; }"
    "QSpinBox:disabled { background-color: {panel}; color: {muted}; border: 1px solid {border}; }"
    "QSpinBox QLineEdit { background: transparent; border: none; padding: 0; color: {textPrimary}; }"
    "QSpinBox::up-button, QSpinBox::down-button { width: 16px; background-color: {panelHover};"
    " border-left: 1px solid {borderSubtle}; }"
    "QSpinBox::up-button { border-top-right-radius: 6px; }"
    "QSpinBox::down-button { border-bottom-right-radius: 6px; }"
    // 同下拉框：三角形 QSS 画不出来，箭头走 PNG（{arrowUp}/{arrowDown} 由调色板换出路径）。
    "QSpinBox::up-arrow { image: url({arrowUp}); width: 8px; height: 8px; }"
    "QSpinBox::down-arrow { image: url({arrowDown}); width: 8px; height: 8px; }";

constexpr const char *kCheckCss =
    "QCheckBox { color: {textPrimary}; background: transparent; spacing: 8px; font-size: 13px; }"
    "QCheckBox:disabled { color: {muted}; }"
    "QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid {border};"
    " border-radius: 4px; background-color: {panel}; }"
    "QCheckBox::indicator:hover, QCheckBox::indicator:checked { border-color: {accent}; }"
    "QCheckBox::indicator:checked { background-color: {accent}; }";

constexpr const char *kRadioCss =
    "QRadioButton { color: {textPrimary}; background: transparent; spacing: 8px; font-size: 13px; }"
    "QRadioButton:disabled { color: {muted}; }"
    "QRadioButton::indicator { width: 16px; height: 16px; border: 1px solid {border};"
    " border-radius: 8px; background-color: {panel}; }"
    "QRadioButton::indicator:hover, QRadioButton::indicator:checked { border-color: {accent}; }"
    "QRadioButton::indicator:checked { background-color: {accent}; border-width: 2px; }";

}

void decorateFieldsIn(QWidget *root)
{
    if (root == nullptr) {
        return;
    }
    const QList<QLineEdit *> edits = root->findChildren<QLineEdit *>();
    for (QLineEdit *edit : edits) {
        // QSpinBox / QComboBox 内部那个 QLineEdit 是它们自己的零件：给它也挂一份
        // "带边框圆角"的模板，表现就是"端口输入框外面一个框、里面又一个框"。
        // 旋钮的外观由 QSpinBox 那条模板负责（里面已经把 QSpinBox QLineEdit 设成透明无边框）。
        if (qobject_cast<QSpinBox *>(edit->parentWidget()) != nullptr
            || qobject_cast<QComboBox *>(edit->parentWidget()) != nullptr) {
            continue;
        }
        decorateField(edit, QString::fromLatin1(kLineEditCss));
    }
    const QList<QSpinBox *> spins = root->findChildren<QSpinBox *>();
    for (QSpinBox *spin : spins) {
        decorateField(spin, QString::fromLatin1(kSpinBoxCss));
    }
    const QList<QCheckBox *> checks = root->findChildren<QCheckBox *>();
    for (QCheckBox *check : checks) {
        decorateField(check, QString::fromLatin1(kCheckCss));
    }
    const QList<QRadioButton *> radios = root->findChildren<QRadioButton *>();
    for (QRadioButton *radio : radios) {
        decorateField(radio, QString::fromLatin1(kRadioCss));
    }
    decorateComboBoxesIn(root);
}

}

