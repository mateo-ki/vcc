#include "gui/AboutDialog.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>

#include "app/AppContext.h"
#include "core/Version.h"
#include "gui/UiText.h"
#include "net/HttpClient.h"
#include "proxy/ProxyManager.h"

namespace Gui {

namespace {

constexpr const char kRepository[] = "https://github.com/mateo-ki/vcc";

QString field(const QString &name, const QString &value)
{
    return QStringLiteral("%1：%2").arg(name, value);
}

QString megabytes(quint64 bytes)
{
    return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
}

} // namespace

QString AboutDialog::repositoryUrl()
{
    return QString::fromLatin1(kRepository);
}

QString AboutDialog::releasesUrl()
{
    return repositoryUrl() + QStringLiteral("/releases");
}

QString AboutDialog::latestReleaseApiUrl()
{
    return QStringLiteral("https://api.github.com/repos/mateo-ki/vcc/releases/latest");
}

QString AboutDialog::updateStatusText(const QString &latestVersion, const QString &currentVersion)
{
    const QString latest = Core::releaseVersion(latestVersion);
    if (Core::isUpdateAvailable(latest, currentVersion)) {
        return QStringLiteral("发现新版本 %1（当前 %2）").arg(latest, currentVersion);
    }
    if (latest == Core::releaseVersion(currentVersion)) {
        return QStringLiteral("已是最新版 %1").arg(currentVersion);
    }
    // 本机跑的是比发布版更新的构建（开发版），这不叫"落后"，也不该说成"已是最新"。
    return QStringLiteral("当前 %1 比最新发布版 %2 更新").arg(currentVersion, latest);
}

QString AboutDialog::failureStatusText(const QString &reason)
{
    // Releases/latest 对"一个发布都还没建"就是 404。这不是故障，直接说人话，
    // 免得上面那串 HTTP 状态码被当成"更新功能坏了"。
    if (reason.startsWith(QStringLiteral("HTTP 404"))) {
        return QStringLiteral("还没有已发布的版本");
    }
    return QStringLiteral("检查失败：%1").arg(reason.isEmpty() ? QStringLiteral("未知原因") : reason);
}

QString AboutDialog::downloadTarget(const QString &directory, const QString &assetName,
                                    const QString &version)
{
    return QDir(directory).filePath(Core::safeInstallerFileName(assetName, version));
}

AboutDialog::AboutDialog(App::AppContext &context, QWidget *parent)
    : QDialog(parent), context_(context)
{
    setWindowTitle(QStringLiteral("关于 VCC"));
    setModal(false);
    resize(520, 330);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("VCC"), this);
    title->setObjectName(QStringLiteral("Title"));
    title->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700;"));
    root->addWidget(title);

    auto *subtitle = new QLabel(UiText::brandSubtitle(), this);
    subtitle->setObjectName(QStringLiteral("Subtitle"));
    root->addWidget(subtitle);

    auto *lines = new QVBoxLayout();
    lines->setSpacing(2);
    for (const QString &line : {field(QStringLiteral("版本"), Core::appVersion()),
                                field(QStringLiteral("开源仓库"), repositoryUrl()),
                                field(QStringLiteral("许可证"), QStringLiteral("Apache-2.0")),
                                QStringLiteral("本开源项目已链接并认可 LINUX DO 社区")}) {
        auto *label = new QLabel(line, this);
        label->setObjectName(QStringLiteral("Body"));
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        lines->addWidget(label);
    }
    root->addLayout(lines);
    root->addSpacing(6);

    statusLabel_ = new QLabel(QStringLiteral("还没检查过更新"), this);
    statusLabel_->setObjectName(QStringLiteral("Body"));
    statusLabel_->setWordWrap(true);
    root->addWidget(statusLabel_);

    auto *actions = new QHBoxLayout();
    actions->setSpacing(8);
    checkButton_ = new QPushButton(QStringLiteral("检查更新"), this);
    downloadButton_ = new QPushButton(QStringLiteral("下载新版本"), this);
    openPageButton_ = new QPushButton(QStringLiteral("打开发布页"), this);
    openFolderButton_ = new QPushButton(QStringLiteral("打开所在文件夹"), this);
    auto *closeButton = new QPushButton(QStringLiteral("关闭"), this);
    downloadButton_->setEnabled(false);
    openPageButton_->setEnabled(false);
    openFolderButton_->setEnabled(false);
    actions->addWidget(checkButton_);
    actions->addWidget(downloadButton_);
    actions->addWidget(openPageButton_);
    actions->addWidget(openFolderButton_);
    actions->addStretch();
    actions->addWidget(closeButton);
    root->addLayout(actions);

    connect(checkButton_, &QPushButton::clicked, this, [this]() { checkForUpdates(); });
    connect(downloadButton_, &QPushButton::clicked, this, [this]() { startDownload(); });
    connect(openPageButton_, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl(release_.url.isEmpty() ? releasesUrl() : release_.url));
    });
    connect(openFolderButton_, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(savedPath_).absolutePath()));
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
}

QString AboutDialog::subscriptionProxy() const
{
    // 内核在跑就顺着它出去，跟订阅拉取用同一个出口；没跑就直连。
    const auto status = context_.proxy().status();
    return status.localProxyActive() && status.mixedPort > 0
        ? QStringLiteral("http://127.0.0.1:%1").arg(status.mixedPort)
        : QString();
}

void AboutDialog::showUpdateResult(const QString &text, const Core::Release &release)
{
    release_ = release;
    statusLabel_->setText(text);
    openPageButton_->setEnabled(!release_.url.isEmpty());
    const bool newer = Core::isUpdateAvailable(release_.version, Core::appVersion());
    downloadButton_->setEnabled(newer && !release_.assetUrl.isEmpty());
    if (newer && release_.assetUrl.isEmpty()) {
        // 有新版但这条发布没挂安装包：说清楚，别让"下载"那颗莫名其妙地灰着。
        statusLabel_->setText(QStringLiteral("%1（这条发布没有可下载的安装包）").arg(text));
    }
}

void AboutDialog::checkForUpdates()
{
    if (checking_) {
        return;
    }
    checking_ = true;
    checkButton_->setEnabled(false);
    downloadButton_->setEnabled(false);
    openPageButton_->setEnabled(false);
    statusLabel_->setText(QStringLiteral("正在检查更新…"));

    const QString proxyUrl = subscriptionProxy();
    auto outcome = std::make_shared<Result<QString>>(
        Result<QString>::withError(QStringLiteral("not run")));
    auto release = std::make_shared<Core::Release>();

    context_.runAsync(
        [proxyUrl, outcome, release]() {
            Net::Request request;
            request.url = AboutDialog::latestReleaseApiUrl();
            request.method = QByteArrayLiteral("GET");
            request.userAgent = QStringLiteral("vcc updater");
            request.headers.append({QStringLiteral("Accept"),
                                    QStringLiteral("application/vnd.github+json")});
            request.connectTimeoutMs = 6000;
            request.timeoutMs = 15000;
            request.proxyUrl = proxyUrl;

            const Result<Net::Response> sent = Net::send(request);
            if (sent.isErr()) {
                *outcome = Result<QString>::withError(sent.error());
                return;
            }
            if (!sent.value().isSuccess()) {
                // 404 = 这个仓库还没有 release，其余状态码原样报出去。
                *outcome = Result<QString>::withError(
                    QStringLiteral("HTTP %1").arg(sent.value().status));
                return;
            }
            const Result<Core::Release> parsed =
                Core::releaseFromJsonText(QString::fromUtf8(sent.value().body));
            if (parsed.isErr()) {
                *outcome = Result<QString>::withError(parsed.error());
                return;
            }
            *release = parsed.value();
            *outcome = Result<QString>::withValue(QStringLiteral("ok"));
        },
        [this, outcome, release]() {
            checking_ = false;
            checkButton_->setEnabled(true);
            if (outcome->isErr()) {
                statusLabel_->setText(failureStatusText(outcome->error()));
                return;
            }
            showUpdateResult(updateStatusText(release->version, Core::appVersion()), *release);
        },
        this);
}

void AboutDialog::startDownload()
{
    if (downloading_ || release_.assetUrl.isEmpty()) {
        return;
    }
    const QString directory
        = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("updates"));
    const QString fileName = Core::safeInstallerFileName(release_.assetName, release_.version);
    const QString target = QDir(directory).filePath(fileName);

    if (QFile::exists(target)) {
        savedPath_ = target;
        openFolderButton_->setEnabled(true);
        statusLabel_->setText(QStringLiteral("已经下载过了：%1").arg(target));
        return;
    }

    downloading_ = true;
    downloadButton_->setEnabled(false);
    statusLabel_->setText(release_.assetSize > 0
        ? QStringLiteral("正在下载 %1（约 %2）…").arg(fileName, megabytes(release_.assetSize))
        : QStringLiteral("正在下载 %1…").arg(fileName));

    const QString proxyUrl = subscriptionProxy();
    const QString assetUrl = release_.assetUrl;
    const quint64 expected = release_.assetSize;
    auto outcome = std::make_shared<Result<QString>>(
        Result<QString>::withError(QStringLiteral("not run")));

    context_.runAsync(
        [assetUrl, target, directory, proxyUrl, expected, outcome]() {
            QDir().mkpath(directory);

            Net::Request request;
            request.url = assetUrl;
            request.method = QByteArrayLiteral("GET");
            request.userAgent = QStringLiteral("vcc updater");
            request.connectTimeoutMs = 10000;
            // 安装包是几十 MB，别拿接口查询那 15s 的预算去卡它。
            request.timeoutMs = 300000;
            request.proxyUrl = proxyUrl;

            const Result<Net::Response> sent = Net::send(request);
            if (sent.isErr()) {
                *outcome = Result<QString>::withError(sent.error());
                return;
            }
            if (!sent.value().isSuccess()) {
                *outcome = Result<QString>::withError(
                    QStringLiteral("HTTP %1").arg(sent.value().status));
                return;
            }
            const QByteArray body = sent.value().body;
            if (expected > 0 && quint64(body.size()) != expected) {
                // GitHub 的重定向或半截响应都可能给短包，落盘前先对一眼长度。
                *outcome = Result<QString>::withError(QStringLiteral("长度不符"));
                return;
            }
            QFile file(target);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                *outcome = Result<QString>::withError(file.errorString());
                return;
            }
            if (file.write(body) != body.size()) {
                file.close();
                QFile::remove(target);
                *outcome = Result<QString>::withError(QStringLiteral("写入不完整"));
                return;
            }
            file.close();
            *outcome = Result<QString>::withValue(target);
        },
        [this, outcome]() {
            downloading_ = false;
            if (outcome->isErr()) {
                downloadButton_->setEnabled(false);
                statusLabel_->setText(QStringLiteral("下载失败：%1").arg(outcome->error()));
                return;
            }
            savedPath_ = outcome->value();
            openFolderButton_->setEnabled(true);
            // 到此为止：装不装由人自己点这个文件，程序不替他起安装器。
            statusLabel_->setText(QStringLiteral("下载完成：%1").arg(savedPath_));
        },
        this);
}

} // namespace Gui
