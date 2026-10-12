#include "gui/SwitchLogDialog.h"

#include "core/TimeUtil.h"
#include "gui/Theme.h"
#include "gui/UiText.h"
#include "gui/Widgets.h"

#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

namespace Gui {

SwitchLogDialog::SwitchLogDialog(const QVector<DbTelemetry::SwitchRow> &rows, qint64 today,
                                 qint64 total, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(UiText::switchLogTitle());
    resize(660, 420);
    setAttribute(Qt::WA_DeleteOnClose, true);

    auto *box = new QVBoxLayout(this);
    box->setContentsMargins(14, 12, 14, 12);
    box->setSpacing(6);

    auto *summary = new QLabel(UiText::switchLogSummary()
                                   .arg(total)
                                   .arg(today)
                                   .arg(DbTelemetry::kRetentionSwitchesDays), this);
    summary->setStyleSheet(QStringLiteral("font-weight: 700;"));
    box->addWidget(summary);

    if (rows.isEmpty()) {
        box->addWidget(new QLabel(UiText::switchLogEmpty(), this));
        return;
    }

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setMaximumHeight(340);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *list = new QWidget(scroll);
    auto *listBox = new QVBoxLayout(list);
    listBox->setContentsMargins(0, 0, 0, 0);
    listBox->setSpacing(4);
    scroll->setWidget(list);
    box->addWidget(scroll);

    const qint64 hidden = qMax<qint64>(0, total - rows.size());
    if (hidden > 0) {
        auto *more = new QLabel(UiText::switchLogHidden().arg(hidden), list);
        more->setObjectName(QStringLiteral("Muted"));
        Gui::Theme::setWidgetCss(more, QStringLiteral("color: {muted}; font-size: 11px;"));
        listBox->addWidget(more);
    }

    // 行是按时间倒序取来的，同一个本地日必然连成一段 —— 只需要"和上一条日期不同就插
    // 一条日头"，不用分桶（Rust mod.rs:3266-3281）。
    const QString todayDay = TimeUtil::localDay(TimeUtil::nowUtcNanos());
    QString currentDay;
    for (const DbTelemetry::SwitchRow &row : rows) {
        const QString day = DbTelemetry::switchDay(row.at);
        if (day != currentDay) {
            currentDay = day;
            auto *head = new QLabel(day == todayDay ? UiText::switchLogTodayDay().arg(day) : day,
                                    list);
            head->setObjectName(QStringLiteral("Subtitle"));
            Gui::Theme::setWidgetCss(head,
                QStringLiteral("color: {textSecondary}; font-size: 11px; font-weight: 700;"));
            listBox->addWidget(head);
        }
        auto *line = new QHBoxLayout();
        line->setSpacing(8);
        auto *clock = new QLabel(clockOf(row.at), list);
        Gui::Theme::setWidgetCss(clock, QStringLiteral("color: {muted}; font-family: monospace; font-size: 11px;"));
        line->addWidget(clock);

        const QString from = row.fromName.isEmpty() ? QStringLiteral("—") : row.fromName;
        // 空 to 是"判定要切但一个替补都没找到"，写成"—"会被当成显示不全。
        QString to = row.toName;
        if (to.isEmpty()) {
            to = row.toNode.isEmpty() ? UiText::switchLogNoSwitch() : row.toNode;
        }
        line->addWidget(new QLabel(QStringLiteral("%1 → %2").arg(from, to), list));

        auto *reason = new QLabel(row.reason, list);
        reason->setObjectName(QStringLiteral("Subtitle"));
        Gui::Theme::setWidgetCss(reason, QStringLiteral("color: {textSecondary}; font-size: 11px;"));
        line->addWidget(reason);

        if (row.latencyMs.has_value()) {
            auto *ms = new QLabel(QStringLiteral("%1 ms").arg(*row.latencyMs), list);
            Gui::Theme::setWidgetCss(ms, QStringLiteral("color: {muted}; font-size: 11px;"));
            line->addWidget(ms);
        }
        line->addStretch();
        listBox->addLayout(line);
    }
    listBox->addStretch();
}

} // namespace Gui
