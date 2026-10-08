#pragma once

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace App {
class AppContext;
}

namespace Gui {

/// 五个页面的工厂：每个页面自己一个 .cpp，MainWindow 只按页签调这里。
/// 返回值所有权交给调用方（QStackedWidget）。
QWidget *createProxyPage(App::AppContext &context, QWidget *parent = nullptr);
QWidget *createNodesPage(App::AppContext &context, QWidget *parent = nullptr);
QWidget *createSourcesPage(App::AppContext &context, QWidget *parent = nullptr);
QWidget *createSettingsPage(App::AppContext &context, QWidget *parent = nullptr);
QWidget *createApiPage(App::AppContext &context, QWidget *parent = nullptr);

}
