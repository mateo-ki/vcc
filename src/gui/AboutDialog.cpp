#include "gui/AboutDialog.h"

#include <QDesktopServices>
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

AboutDialog::AboutDialog(App::AppContext &context, QWidget *parent)
    : QDialog(parent), context_(context)
{
    setWindowTitle(QStringLiteral("关于 VCC"));
    setModal(false);
    resize(460, 300);

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
    openButton_ = new QPushButton(QStringLiteral("打开发布页"), this);
    auto *closeButton = new QPushButton(QStringLiteral("关闭"), this);
    openButton_->setEnabled(false);
    actions->addWidget(checkButton_);
    actions->addWidget(openButton_);
    actions->addStretch();
    actions->addWidget(closeButton);
    root->addLayout(actions);

    connect(checkButton_, &QPushButton::clicked, this, [this]() { checkForUpdates(); });
    connect(openButton_, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl(releaseUrl_.isEmpty() ? releasesUrl() : releaseUrl_));
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
}

void AboutDialog::showUpdateResult(const QString &text, const QString &releaseUrl)
{
    statusLabel_->setText(text);
    releaseUrl_ = releaseUrl;
    openButton_->setEnabled(!releaseUrl_.isEmpty());
}

void AboutDialog::checkForUpdates()
{
    if (checking_) {
        return;
    }
    checking_ = true;
    checkButton_->setEnabled(false);
    openButton_->setEnabled(false);
    statusLabel_->setText(QStringLiteral("正在检查更新…"));

    // 内核在跑就顺着它出去，跟订阅拉取用同一个出口；没跑就直连。
    const auto status = context_.proxy().status();
    const QString proxyUrl = status.localProxyActive() && status.mixedPort > 0
        ? QStringLiteral("http://127.0.0.1:%1").arg(status.mixedPort)
        : QString();

    auto outcome = std::make_shared<Result<QString>>(
        Result<QString>::withError(QStringLiteral("not run")));
    auto latest = std::make_shared<QString>();
    auto page = std::make_shared<QString>();

    context_.runAsync(
        [proxyUrl, outcome, latest, page]() {
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
            const Result<Core::Release> release =
                Core::releaseFromJsonText(QString::fromUtf8(sent.value().body));
            if (release.isErr()) {
                *outcome = Result<QString>::withError(release.error());
                return;
            }
            *latest = release.value().version;
            *page = release.value().url;
            *outcome = Result<QString>::withValue(QStringLiteral("ok"));
        },
        [this, outcome, latest, page]() {
            checking_ = false;
            checkButton_->setEnabled(true);
            if (outcome->isErr()) {
                openButton_->setEnabled(false);
                statusLabel_->setText(failureStatusText(outcome->error()));
                return;
            }
            showUpdateResult(updateStatusText(*latest, Core::appVersion()), *page);
        },
        this);
}

} // namespace Gui
