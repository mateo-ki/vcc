#pragma once

#include <QDialog>
#include <QString>

#include "core/Version.h"

namespace App {
class AppContext;
}

class QLabel;
class QPushButton;

namespace Gui {

/// 「关于」：版本号、开源仓库、许可证，以及"去 GitHub 看一眼有没有新版"。
/// 检查到新版可以**下载**安装包到程序目录下的 updates/，但**不由程序启动它**：
/// 装不装、什么时候装永远是人自己点的动作。
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
    /// 下载落在 <directory>/<安全文件名>；文件名来自远端，先过 Core 的白名单。
    static QString downloadTarget(const QString &directory, const QString &assetName,
                                  const QString &version);

    /// 把检查结果写进界面：只有"确实有新版、且这条 release 挂着安装包"才点亮下载。
    void showUpdateResult(const QString &text, const Core::Release &release);

private:
    void checkForUpdates();
    void startDownload();
    /// 内核在跑就顺着它出去，跟订阅拉取用同一个出口；没跑就直连。
    QString subscriptionProxy() const;

    App::AppContext &context_;
    QLabel *statusLabel_ = nullptr;
    QPushButton *checkButton_ = nullptr;
    QPushButton *downloadButton_ = nullptr;
    QPushButton *openPageButton_ = nullptr;
    QPushButton *openFolderButton_ = nullptr;
    Core::Release release_;
    QString savedPath_;
    bool checking_ = false;
    bool downloading_ = false;
};

} // namespace Gui
