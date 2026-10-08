#pragma once

#include <QWidget>

namespace App {
class AppContext;
}

namespace Gui {

/// 五个页面共同的外壳：持有一份 AppContext，负责自己的布局和刷新。
///
/// Rust 那边是 egui 每帧重建，数据从 BgMsg 通道里取；这里是常驻 widget，
/// 所以"刷新"是一次显式的取数 + 重画，由 AppShell 的轮询定时器驱动。
class Page : public QWidget {
    Q_OBJECT

public:
    explicit Page(App::AppContext &context, QWidget *parent = nullptr);

    /// 从库里/运行态重新取一次数据并重画。切页、后台结果落地、托盘唤醒都走这里。
    virtual void refresh() = 0;

protected:
    App::AppContext &context_;
};

}
