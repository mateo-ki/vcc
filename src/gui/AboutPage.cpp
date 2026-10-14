#include "gui/AboutPage.h"

#include <QByteArray>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>
#include <optional>

#include "app/AppContext.h"
#include "core/Crypto.h"
#include "core/Version.h"
#include "kernel/KernelSource.h"
#include "gui/UiText.h"
#include "net/HttpClient.h"
#include "proxy/ProxyManager.h"
#include "sysproxy/SystemProxy.h"

namespace Gui {

namespace {

constexpr const char kRepository[] = "https://github.com/mateo-ki/vcc";

QString field(const QString &name, const QString &value)
{
    return QStringLiteral("%1：%2").arg(name, value);
}

QString megabytes(qint64 bytes)
{
    return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
}

} // namespace

QString AboutPage::repositoryUrl()
{
    return QString::fromLatin1(kRepository);
}

QString AboutPage::releasesUrl()
{
    return repositoryUrl() + QStringLiteral("/releases");
}

QString AboutPage::latestReleaseApiUrl()
{
    return QStringLiteral("https://api.github.com/repos/mateo-ki/vcc/releases/latest");
}

QString AboutPage::updateStatusText(const QString &latestVersion, const QString &currentVersion)
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

QString AboutPage::failureStatusText(const QString &reason)
{
    // Releases/latest 对"一个发布都还没建"就是 404。这不是故障，直接说人话，
    // 免得上面那串 HTTP 状态码被当成"更新功能坏了"。
    if (reason.startsWith(QStringLiteral("HTTP 404"))) {
        return QStringLiteral("还没有已发布的版本");
    }
    return QStringLiteral("检查失败：%1").arg(reason.isEmpty() ? QStringLiteral("未知原因") : reason);
}

QString AboutPage::downloadTarget(const QString &directory, const QString &assetName,
                                    const QString &version)
{
    return QDir(directory).filePath(Core::safeInstallerFileName(assetName, version));
}

QString AboutPage::progressText(qint64 received, qint64 total)
{
    if (total > 0) {
        const int percent = int(received * 100 / total);
        return QStringLiteral("正在下载 %1 / %2（%3%）")
            .arg(megabytes(received), megabytes(total))
            .arg(percent);
    }
    // 对端没给长度（分块传输）：只报已收，不编一个百分比。
    return QStringLiteral("正在下载 %1…").arg(megabytes(received));
}

AboutPage::AboutPage(App::AppContext &context, QWidget *parent)
    : Page(context, parent)
{
    // 这是一页，不是对话框：没有标题栏，也就没有窗口图标和"关闭"那颗按钮。
    setObjectName(QStringLiteral("AboutPage"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(10);

    // 品牌行：图标 + 名字/副标题，和侧栏顶部那一行同一枚图。
    auto *head = new QHBoxLayout();
    head->setSpacing(10);
    constexpr int kLogoSize = 44;
    auto *logo = new QLabel(this);
    logo->setObjectName(QStringLiteral("AboutLogo"));
    logo->setFixedSize(kLogoSize, kLogoSize);
    logo->setScaledContents(true);
    logo->setPixmap(QPixmap(QStringLiteral(":/app-icon.png")).scaled(
        kLogoSize, kLogoSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    head->addWidget(logo);
    auto *headText = new QVBoxLayout();
    headText->setSpacing(1);
    auto *title = new QLabel(QStringLiteral("VCC"), this);
    title->setObjectName(QStringLiteral("Title"));
    title->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700;"));
    auto *subtitle = new QLabel(UiText::brandSubtitle(), this);
    subtitle->setObjectName(QStringLiteral("Subtitle"));
    headText->addWidget(title);
    headText->addWidget(subtitle);
    head->addLayout(headText);
    head->addStretch();
    root->addLayout(head);

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
    root->addSpacing(4);

    statusLabel_ = new QLabel(QStringLiteral("还没检查过更新"), this);
    statusLabel_->setObjectName(QStringLiteral("Body"));
    statusLabel_->setWordWrap(true);
    root->addWidget(statusLabel_);

    // 进度条平时不占位：只在下载那几秒出现，用真实收到的字节数驱动。
    progressBar_ = new QProgressBar(this);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressBar_->setTextVisible(false);
    progressBar_->setVisible(false);
    root->addWidget(progressBar_);

    // 布局：次要动作靠左、主操作靠右（Windows 那一套），关闭永远在最右。
    // 之前五颗按钮全挤在左边、"关闭"孤零零吊在右头，看着就是歪的。
    auto *actions = new QHBoxLayout();
    actions->setSpacing(8);
    openPageButton_ = new QPushButton(QStringLiteral("打开发布页"), this);
    openFolderButton_ = new QPushButton(QStringLiteral("打开所在文件夹"), this);
    checkButton_ = new QPushButton(QStringLiteral("检查更新"), this);
    downloadButton_ = new QPushButton(QStringLiteral("下载新版本"), this);
    downloadButton_->setEnabled(false);
    openPageButton_->setEnabled(false);
    openFolderButton_->setEnabled(false);
    actions->addWidget(openPageButton_);
    actions->addWidget(openFolderButton_);
    actions->addStretch();
    actions->addWidget(checkButton_);
    actions->addWidget(downloadButton_);
    root->addLayout(actions);

    progressTimer_ = new QTimer(this);
    progressTimer_->setInterval(100);
    connect(progressTimer_, &QTimer::timeout, this, [this]() { pollTransfer(); });
    connect(checkButton_, &QPushButton::clicked, this, [this]() { checkForUpdates(); });
    connect(downloadButton_, &QPushButton::clicked, this, [this]() { startDownload(); });
    connect(openPageButton_, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl(release_.url.isEmpty() ? releasesUrl() : release_.url));
    });
    connect(openFolderButton_, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(savedPath_).absolutePath()));
    });
}

QList<Kernel::Source> AboutPage::downloadChannels(bool proxyAvailable)
{
    QList<Kernel::Source> channels;
    channels << Kernel::Source::GhProxy;
    if (proxyAvailable) {
        channels << Kernel::Source::Proxy;
    }
    channels << Kernel::Source::Official;
    return channels;
}

AboutPage::DownloadPlan AboutPage::planForDownload(Kernel::Source channel, const QString &assetUrl,
                                                   const QString &kernelMixed,
                                                   const QString &systemProxy)
{
    DownloadPlan plan;
    switch (channel) {
    case Kernel::Source::GhProxy: {
        // 镜像前缀与内核下载共用同一张表（Kernel::assetMirrorPrefixes），顺序即优先级。
        const auto &prefixes = Kernel::assetMirrorPrefixes();
        plan.url = prefixes.isEmpty() ? assetUrl : prefixes.first().second + assetUrl;
        // 镜像站本身是公网地址，不需要再套一层代理。
        plan.proxyUrl = QString();
        return plan;
    }
    case Kernel::Source::Proxy:
        plan.url = assetUrl;
        plan.proxyUrl = pickProxyUrl(kernelMixed, systemProxy);
        return plan;
    default:
        plan.url = assetUrl;
        plan.proxyUrl = QString();   // 明确"就是直连"，失败也就直报
        return plan;
    }
}

QString AboutPage::digestHex(const QString &assetDigest)
{
    const QString prefix = QStringLiteral("sha256:");
    if (!assetDigest.startsWith(prefix)) {
        return QString();
    }
    return assetDigest.mid(prefix.size()).toLower();
}

QString AboutPage::pickProxyUrl(const QString &kernelMixed, const QString &systemProxy)
{
    if (!kernelMixed.isEmpty()) {
        return kernelMixed;
    }
    if (!systemProxy.isEmpty()) {
        return systemProxy;
    }
    return QString();   // 两条都没有才直连，让对端的失败原样报给用户
}

void AboutPage::refresh()
{
    // 版本号是编译期常量，检查更新是用户点的，运行态也没有要轮询的东西 ——
    // 但基类要求实现它：切页与后台落地都会调一次，留空就是"这一页不用重画"。
}

QString AboutPage::updateProxyUrl() const
{
    const auto status = context_.proxy().status();
    const QString kernelMixed = status.localProxyActive() && status.mixedPort > 0
        ? QStringLiteral("http://127.0.0.1:%1").arg(status.mixedPort)
        : QString();
    // 注册表查询是阻塞的，但这条链路本身就在 worker 线程上跑，且一次点击最多两次。
    return pickProxyUrl(kernelMixed, SysProxy::currentSystemProxyUrl());
}

void AboutPage::pollTransfer()
{
    if (!transfer_) {
        return;
    }
    const qint64 received = transfer_->received.load();
    const qint64 total = transfer_->total.load();
    if (total > 0) {
        progressBar_->setRange(0, 100);
        progressBar_->setValue(int(received * 100 / total));
    } else {
        progressBar_->setRange(0, 0);   // 长度未知：走马灯，不假装知道百分比
    }
    statusLabel_->setText(progressText(received, total));
}

void AboutPage::showUpdateResult(const QString &text, const Core::Release &release)
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

void AboutPage::checkForUpdates()
{
    if (checking_) {
        return;
    }
    checking_ = true;
    checkButton_->setEnabled(false);
    downloadButton_->setEnabled(false);
    openPageButton_->setEnabled(false);
    statusLabel_->setText(QStringLiteral("正在检查更新…"));

    const QString proxyUrl = updateProxyUrl();
    auto outcome = std::make_shared<Result<QString>>(
        Result<QString>::withError(QStringLiteral("not run")));
    auto release = std::make_shared<Core::Release>();

    context_.runAsync(
        [proxyUrl, outcome, release]() {
            Net::Request request;
            request.url = AboutPage::latestReleaseApiUrl();
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

namespace {

/// 三条通道列成一个列表让人挑。一次只走一条：并发抢跑会把三条线的成败混成一条，
/// 报"下载失败"时没人知道该怪谁（内核下载那边删掉 race 模式就是为这个）。
std::optional<Kernel::Source> chooseChannel(QWidget *parent, const QList<Kernel::Source> &channels,
                                            Kernel::Source current)
{
    QDialog box(parent);
    box.setWindowTitle(QStringLiteral("选择下载方式"));
    auto *layout = new QVBoxLayout(&box);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(10);

    auto *hint = new QLabel(QStringLiteral("下载完成后会用 GitHub 公布的 sha256 校验，对不上就删掉。"),
                            &box);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *list = new QListWidget(&box);
    for (Kernel::Source channel : channels) {
        for (const Kernel::SourceOption &option : Kernel::sourceOptions()) {
            if (option.source != channel) {
                continue;
            }
            auto *item = new QListWidgetItem(option.label, list);
            item->setToolTip(option.hint);
            item->setData(Qt::UserRole, int(channel));
            if (channel == current) {
                list->setCurrentItem(item);
            }
        }
    }
    if (list->currentItem() == nullptr && list->count() > 0) {
        list->setCurrentRow(0);
    }
    list->setMinimumHeight(132);
    layout->addWidget(list, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &box);
    buttons->setStyleSheet(QStringLiteral("font-size: 13px;"));
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("开始下载"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &box, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &box, &QDialog::reject);
    layout->addWidget(buttons);

    if (box.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    QListWidgetItem *item = list->currentItem();
    if (item == nullptr) {
        return std::nullopt;
    }
    return static_cast<Kernel::Source>(item->data(Qt::UserRole).toInt());
}

} // namespace

void AboutPage::startDownload()
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

    const QString kernelMixed = [&] {
        const auto status = context_.proxy().status();
        return status.localProxyActive() && status.mixedPort > 0
            ? QStringLiteral("http://127.0.0.1:%1").arg(status.mixedPort)
            : QString();
    }();
    const QString systemProxy = SysProxy::currentSystemProxyUrl();
    const bool proxyAvailable = !kernelMixed.isEmpty() || !systemProxy.isEmpty();
    const auto chosen = chooseChannel(this, downloadChannels(proxyAvailable), lastChannel_);
    if (!chosen.has_value()) {
        return;   // 取消就是取消，不许偷偷用默认通道开跑
    }
    lastChannel_ = *chosen;
    const DownloadPlan plan = planForDownload(*chosen, release_.assetUrl, kernelMixed, systemProxy);
    const QString expectedSha = digestHex(release_.assetDigest);
    if (plan.url != release_.assetUrl && expectedSha.isEmpty()) {
        // 镜像站只是把字节搬过来，它同时也有能力换掉字节。没有 GitHub 给的校验值，
        // 就没有任何东西能证明落盘的那个 exe 是 GitHub 上那个。
        statusLabel_->setText(QStringLiteral("这条发布没有公布 sha256，镜像下载已拒绝，请选直连或本地代理。"));
        return;
    }

    downloading_ = true;
    downloadButton_->setEnabled(false);
    checkButton_->setEnabled(false);
    transfer_ = std::make_shared<Transfer>();
    transfer_->total.store(qint64(release_.assetSize));
    progressBar_->setRange(0, 0);
    progressBar_->setValue(0);
    progressBar_->setVisible(true);
    statusLabel_->setText(progressText(0, qint64(release_.assetSize)));
    progressTimer_->start();

    const QString assetUrl = plan.url;
    const QString proxyUrl = plan.proxyUrl;
    const qint64 expected = qint64(release_.assetSize);
    auto outcome = std::make_shared<Result<QString>>(
        Result<QString>::withError(QStringLiteral("not run")));

    context_.runAsync(
        [assetUrl, target, directory, proxyUrl, expected, expectedSha, outcome,
         transfer = transfer_]() {
            QDir().mkpath(directory);

            Net::Request request;
            request.url = assetUrl;
            request.method = QByteArrayLiteral("GET");
            request.userAgent = QStringLiteral("vcc updater");
            request.connectTimeoutMs = 10000;
            // 安装包是几十 MB，别拿接口查询那 15s 的预算去卡它。
            request.timeoutMs = 600000;
            request.proxyUrl = proxyUrl;
            // 边收边写进文件：不把 23MB 整块读进内存，进度也就有真数可报。
            request.downloadToPath = target;
            request.downloadProgress = [transfer](qint64 received, qint64 total) {
                transfer->received.store(received);
                if (total > 0) {
                    transfer->total.store(total);
                }
            };

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
            // 先校验再谈成功：字节数对但内容被换过（镜像/中间人）也必须当失败。
            if (!expectedSha.isEmpty()) {
                const Result<QString> hash = Crypto::sha256HexOfFile(target);
                if (hash.isErr()) {
                    *outcome = Result<QString>::withError(hash.error());
                    QFile::remove(target);
                    return;
                }
                if (hash.value().toLower() != expectedSha) {
                    *outcome = Result<QString>::withError(QStringLiteral("sha256 不匹配"));
                    QFile::remove(target);
                    return;
                }
            }
            const qint64 onDisk = QFileInfo(target).size();
            if (expected > 0 && onDisk != expected) {
                // 落盘字节数和发布元数据对不上就当失败：残包由 net 层删掉。
                *outcome = Result<QString>::withError(
                    QStringLiteral("落盘 %1，应为 %2").arg(onDisk).arg(expected));
                return;
            }
            *outcome = Result<QString>::withValue(target);
        },
        [this, outcome]() {
            downloading_ = false;
            progressTimer_->stop();
            progressBar_->setVisible(false);
            checkButton_->setEnabled(true);
            if (outcome->isErr()) {
                downloadButton_->setEnabled(true);
                statusLabel_->setText(QStringLiteral("下载失败：%1，可以再点一次").arg(outcome->error()));
                return;
            }
            progressBar_->setRange(0, 100);
            progressBar_->setValue(100);
            savedPath_ = outcome->value();
            openFolderButton_->setEnabled(true);
            // 到此为止：装不装由人自己点这个文件，程序不替他起安装器。
            statusLabel_->setText(QStringLiteral("下载完成：%1").arg(savedPath_));
        },
        this);
}

} // namespace Gui
