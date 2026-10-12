#pragma once

#include <QDialog>
#include <QVector>

#include "db/DatabaseTelemetry.h"

namespace Gui {

/// 出口切换记录窗口，对应 Rust `gui/mod.rs::ui_switch_log`：按本地日分组列出保留期内的
/// 全部历史 —— 时间、从谁到谁、为什么、切完测到的延迟。
class SwitchLogDialog : public QDialog {
public:
    SwitchLogDialog(const QVector<DbTelemetry::SwitchRow> &rows, qint64 today, qint64 total,
                    QWidget *parent = nullptr);

};

} // namespace Gui
