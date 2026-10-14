#pragma once

#include <QString>

#include <atomic>
#include <memory>

#include "core/Version.h"
#include "gui/Page.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

namespace Gui {

/// 「关于」页：版本号、开源仓库、许可证，以及"去 GitHub 看一眼有没有新版"。
/// 它是侧栏的第六个页签（Rust 的侧栏只有五枚，这是有意多出的一行）。
/// 检查到新版可以下载安装包到程序目录下的 updates/（流式落盘 + 真进度），
/// 但**不由程序启动它**：装不装、什么时候装永远是人自己点的动作。
class AboutPage : public Page {
    Q_OBJECT

public:
    explicit AboutPage(App::AppContext &context, QWidget *parent = nullptr);

    /// 这一页没有要轮询的运行态：版本是编译期常量，检查更新是用户点的。
    void refresh() override;

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
    /// 进度条上那行字（收了多少 / 一共多少；总长未知时只报已收，不编百分比）。
    static QString progressText(qint64 received, qint64 total);
    /// 出口优先级：内核 mixed > 系统代理 > 直连。GitHub 的资产 CDN 在这台机器上直连
    /// 实测 0 字节 / 20s 就断，而"检查更新"多半是在内核没跑的时候点的。
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
    /// 这条链路（检查 + 下载）该走哪个出口：自己的内核 mixed 端口优先，其次系统代理。
    QString updateProxyUrl() const;
    /// 下载期间每 100ms 读一次 worker 写的计数：进度只有这一个来源，
    /// 不让 worker 直接跨线程碰控件，也不往事件队列里灌几千个 posted event。
    void pollTransfer();

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
