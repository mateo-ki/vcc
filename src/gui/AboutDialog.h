#pragma once

#include <QDialog>
#include <QString>

#include <atomic>
#include <memory>

#include "core/Version.h"

namespace App {
class AppContext;
}

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

namespace Gui {

/// 「关于」：版本号、开源仓库、许可证，以及"去 GitHub 看一眼有没有新版"。
/// 检查到新版可以**下载**安装包到程序目录下的 updates/（流式落盘 + 真进度条），
/// 但**不由程序启动它**：装不装、什么时候装永远是人自己点的动作。
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
    /// 进度条上那行字（纯函数：收了多少 / 一共多少，总长未知时只报已收）。
    static QString progressText(qint64 received, qint64 total);

    /// 出口优先级：内核 mixed > 系统代理 > 直连。GitHub 的资产 CDN 在这台机器上直连
    /// 实测 0 字节 / 20s 就断，而"检查更新"多半是在内核没跑的时候点的，只认自己的
    /// mixed 端口就等于必然失败。
    static QString pickProxyUrl(const QString &kernelMixed, const QString &systemProxy);

    /// 把检查结果写进界面：只有"确实有新版、且这条发布真挂了安装包"才点亮下载。
    void showUpdateResult(const QString &text, const Core::Release &release);

private:
    struct Transfer {
        std::atomic<qint64> received{ 0 };
        std::atomic<qint64> total{ 0 };
    };

    void checkForUpdates();
    void startDownload();
    /// 这条链路（检查 + 下载）该走哪个出口：自己的内核 mixed 端口优先，
    /// 内核没跑就用 Windows 系统代理 —— 见 pickProxyUrl。
    QString updateProxyUrl() const;

    /// 下载期间每 100ms 读一次 worker 写的计数：进度只有这一个来源，
    /// 不让 worker 直接跨线程碰控件，也不往事件队列里灌几千个 posted event。
    void pollTransfer();

    App::AppContext &context_;
    QLabel *statusLabel_ = nullptr;
    QProgressBar *progressBar_ = nullptr;
    QPushButton *checkButton_ = nullptr;
    QPushButton *downloadButton_ = nullptr;
    QPushButton *openPageButton_ = nullptr;
    QPushButton *openFolderButton_ = nullptr;
    QTimer *progressTimer_ = nullptr;
    std::shared_ptr<Transfer> transfer_;
    Core::Release release_;
    QString savedPath_;
    bool checking_ = false;
    bool downloading_ = false;
};

} // namespace Gui
