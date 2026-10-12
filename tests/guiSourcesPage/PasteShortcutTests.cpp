#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>

#include <memory>

#include "app/AppContext.h"
#include "app/MainWindow.h"
#include "db/Database.h"
#include "gui/SourcesPage.h"
#include "models/Source.h"

using namespace Gui;

namespace {

std::shared_ptr<App::AppContext> makeContext(const QString &directory)
{
    App::AppContext::Options options;
    options.dbPath = QDir(directory).filePath(QStringLiteral("vpn-parser.db"));
    const Result<std::shared_ptr<App::AppContext>> created = App::AppContext::create(options);
    if (created.isErr()) {
        ADD_FAILURE() << created.error().toStdString();
        return nullptr;
    }
    return created.value();
}

int sourceCount(App::AppContext &context)
{
    const Result<QVector<Models::Source>> sources = context.database().listSources();
    return sources.isOk() ? sources.value().size() : -1;
}

/// 真发一次 Ctrl+V：走 QApplication::sendEvent，应用级事件过滤器才有机会介入
/// （直接调 importFromClipboard 就绕过了要验的那一层）。
void sendPasteTo(QWidget *target)
{
    QKeyEvent event(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier, QStringLiteral("v"));
    QApplication::sendEvent(target, &event);
}


}

/// 他要的是"在任何一页粘贴都能导入"。判据挂在真主窗口上：把 Ctrl+V 发给窗口本身
/// （不是输入框），JSON 分支应该建出一个源，且全程不打网络。
TEST(SourcesPaste, clipboardJsonImportsFromAnywhereInTheWindow)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    MainWindow window(*context);
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();
    ASSERT_EQ(sourceCount(*context), 0);

    QApplication::clipboard()->setText(
        QStringLiteral("[{\"name\":\"paste-check\",\"url_template\":\"http://127.0.0.1:9/sub\","
                       "\"enabled\":true}]"));
    // 停在控制台页（不是订阅源页）也一样要能导入 —— 这就是"任何一页"。
    window.selectTab(Tab::Proxy);
    QCoreApplication::processEvents();

    sendPasteTo(&window);
    QCoreApplication::processEvents();

    EXPECT_EQ(sourceCount(*context), 1) << "窗口里按 Ctrl+V 应该把剪贴板里的订阅源 JSON 建出来";
}

/// 反向的那一半：输入框里的 Ctrl+V 必须还是"把文字粘进框里"，不能被全局导入抢走。
/// 用 QShortcut 做全局导入时这条会红 —— 快捷键命中就吞键，谁也没干活
/// （原来 PasteShortcutTests 里记的就是这个故障）。
TEST(SourcesPaste, ctrlVStillPastesIntoLineEdits)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);

    MainWindow window(*context);
    auto *edit = new QLineEdit(&window);
    window.show();
    QCoreApplication::processEvents();
    ASSERT_EQ(sourceCount(*context), 0);

    QApplication::clipboard()->setText(QStringLiteral("hk-01"));
    edit->setFocus();
    QCoreApplication::processEvents();
    ASSERT_EQ(window.focusWidget(), edit);

    // 万一豁免被人删掉，Ctrl+V 会走到导入逻辑并对着一串普通文字弹模态框 —— 那会让这条
    // 测试**挂住**而不是报红。预先排一次"有模态框就点掉"，让回归以断言失败的形式露出来。
    QTimer::singleShot(0, qApp, []() {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (box != nullptr) {
            const QList<QAbstractButton *> buttons = box->buttons();
            if (!buttons.isEmpty()) {
                buttons.first()->click();
            } else {
                box->done(QDialog::Accepted);
            }
        }
    });

    sendPasteTo(edit);
    QCoreApplication::processEvents();

    EXPECT_EQ(edit->text().toStdString(), std::string("hk-01")) << "Ctrl+V 被全局导入抢走了";
    EXPECT_EQ(sourceCount(*context), 0) << "往输入框粘贴文字不该顺手建出订阅源";
}
