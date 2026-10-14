#pragma once

#include <QDialog>
#include <QString>

namespace App {
class AppContext;
}

class QLabel;
class QPushButton;

namespace Gui {

/// 「关于」：版本号、开源仓库、许可证，以及"去 GitHub 看一眼有没有新版"。
/// 这里只做**问一声**：拉 Releases/latest 比个版本号，然后把发布页地址给用户。
/// 不静默下载、不自动装 —— 装上这一步永远留给他自己点。
class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(App::AppContext &context, QWidget *parent = nullptr);

    /// 项目仓库与它的发布页（界面上的链接和检查更新共用同一处定义）。
    static QString repositoryUrl();
    static QString releasesUrl();
    /// GitHub 的"最新 release"接口地址。
    static QString latestReleaseApiUrl();

    /// 检查完之后界面该说什么（纯函数，测试直接钉它，不必联网）。
    static QString updateStatusText(const QString &latestVersion, const QString &currentVersion);
    static QString failureStatusText(const QString &reason);

    /// 把检查结果写进界面：有新版时把"打开发布页"那颗按钮指过去。
    void showUpdateResult(const QString &text, const QString &releaseUrl);

private:
    void checkForUpdates();

    App::AppContext &context_;
    QLabel *statusLabel_ = nullptr;
    QPushButton *checkButton_ = nullptr;
    QPushButton *openButton_ = nullptr;
    QString releaseUrl_;
    bool checking_ = false;
};

} // namespace Gui
