#include "gui/ApiPage.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

#include <nlohmann/json.hpp>

#include "api/ApiServerController.h"
#include "app/AppContext.h"
#include "core/Log.h"
#include "db/Database.h"
#include "db/DatabaseExport.h"
#include "gui/Pages.h"
#include "gui/Theme.h"
#include "gui/UiText.h"
#include "net/HttpClient.h"
#include "platform/ProcessControl.h"

namespace Gui {

namespace {

static QString normalizeApiBase(QString raw)
{
    raw = raw.trimmed();
    while (raw.endsWith(QLatin1Char('/'))) {
        raw.chop(1);
    }
    if (!raw.isEmpty() && !raw.startsWith(QStringLiteral("http://")) && !raw.startsWith(QStringLiteral("https://"))) {
        raw = QStringLiteral("https://") + raw;
    }
    return raw;
}

struct ApiItem {
    const char *method;
    const char *path;
    const char *desc;
    bool openable;
};

const ApiItem kApiItems[] = {
    {"GET", "/api/health", "健康检查", true},
    {"GET", "/api/stats", "统计信息", true},
    {"GET", "/api/settings", "读取设置", true},
    {"PUT", "/api/settings", "更新设置", false},
    {"GET", "/api/sources", "订阅源列表", true},
    {"POST", "/api/sources", "创建订阅源", false},
    {"DELETE", "/api/sources/{id}", "删除订阅源及关联节点", false},
    {"POST", "/api/sources/fetch-all", "拉取全部启用源", false},
    {"GET", "/api/nodes?page=1&sort=latency", "节点分页查询", true},
    {"POST", "/api/nodes/speedtest", "自动内核真实代理批量测速，可传 ids 只测本页", false},
    {"GET", "/api/kernels", "检测 Mihomo、Xray、Sing-box 路径和版本", true},
    {"GET", "/api/proxy/status", "读取代理运行状态", true},
    {"GET", "/api/proxy/config", "读取 TUN、DNS 与分流规则配置", true},
    {"PUT", "/api/proxy/config", "更新并持久化代理配置", false},
    {"GET", "/api/proxy/log", "读取当前代理内核最近日志", true},
    {"POST", "/api/proxy/start", "按 node_id 自动选择内核启动（系统代理或 TUN）", false},
    {"POST", "/api/proxy/stop", "停止当前代理内核并恢复系统代理", false},
    {"POST", "/api/proxy/restart", "按 node_id 重启代理", false},
    {"POST", "/api/nodes/delete-dead", "删除超时/失败节点", false},
    {"POST", "/api/nodes/delete-invalid", "删除 127.0.0.1/内网/localhost 等无效主机", false},
    {"POST", "/api/nodes/dedupe", "去除重复节点（同协议/地址/端口/认证）", false},
    {"GET", "/api/export/json?quality_only=true&max_latency_ms=500&min_download_kbps=0&limit=50", "导出 TopN TCP 可用节点 JSON", true},
    {"GET", "/api/export/clash?quality_only=true&max_latency_ms=500&min_download_kbps=0&limit=50", "导出 TopN Clash 配置", true},
    {"GET", "/api/export/uri?quality_only=true&max_latency_ms=500&min_download_kbps=0&limit=50&base64=true", "导出 TopN 订阅（Base64，v2rayN 用）", true},
    {"GET", "/api/export/uri?quality_only=true&max_latency_ms=300&limit=30&base64=true", "更严格：Top30 + 延迟≤300ms（v2rayN）", true},
    {"GET", "/api/export/clash?quality_only=false", "导出全部节点 Clash（关闭过滤）", true},
};

} // namespace

ApiPage::ApiPage(App::AppContext &context, QWidget *parent)
    : Page(context, parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 14, 18, 14);
    root->setSpacing(10);

    root->addWidget(buildHeader());
    root->addWidget(buildServerStatusCard());
    root->addWidget(buildPublisherCard());

    auto *endpoints = buildEndpointsSection();
    root->addWidget(endpoints, 1);

    refresh();
}

QWidget *ApiPage::buildHeader()
{
    auto *header = new QWidget(this);
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *left = new QVBoxLayout();
    left->setSpacing(2);

    auto *title = new QLabel(QStringLiteral("HTTP API"), header);
    QFont titleFont = title->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    title->setFont(titleFont);
    left->addWidget(title);

    auto *subtitle = new QLabel(QStringLiteral("点击路径复制完整 URL · GET 可浏览器打开"), header);
    subtitle->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::current().muted.name()));
    left->addWidget(subtitle);

    layout->addLayout(left);
    layout->addStretch();

    statusFeedbackLabel_ = new QLabel(header);
    statusFeedbackLabel_->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;").arg(Theme::current().accentStrong.name()));
    layout->addWidget(statusFeedbackLabel_);

    return header;
}

QWidget *ApiPage::buildServerStatusCard()
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("ServerStatusCard"));
    Theme::decorateCard(card);

    auto *layout = new QHBoxLayout(card);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(12);

    statusBadge_ = new QLabel(card);
    layout->addWidget(statusBadge_);

    listenAddrLabel_ = new QLabel(card);
    layout->addWidget(listenAddrLabel_);

    tokenContainer_ = new QWidget(card);
    auto *tokenLayout = new QHBoxLayout(tokenContainer_);
    tokenLayout->setContentsMargins(0, 0, 0, 0);
    tokenLayout->setSpacing(4);

    auto *tokenPrefix = new QLabel(QStringLiteral("访问令牌:"), tokenContainer_);
    tokenPrefix->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::current().muted.name()));
    tokenLayout->addWidget(tokenPrefix);

    tokenLabel_ = new QPushButton(tokenContainer_);
    tokenLabel_->setFlat(true);
    tokenLabel_->setCursor(Qt::PointingHandCursor);
    tokenLabel_->setToolTip(QStringLiteral("所有接口都需要它；只读 GET 可用 ?token= 传"));
    tokenLabel_->setStyleSheet(QStringLiteral(
        "QPushButton { text-align: left; background: transparent; border: none; color: %1; font-weight: bold; padding: 0; } "
        "QPushButton:hover { text-decoration: underline; }")
        .arg(Theme::current().textPrimary.name()));
    connect(tokenLabel_, &QPushButton::clicked, this, [this]() {
        copyToClipboard(tokenLabel_->text(), QStringLiteral("已复制访问令牌"));
    });
    tokenLayout->addWidget(tokenLabel_);
    layout->addWidget(tokenContainer_);

    layout->addStretch();

    startBtn_ = new QPushButton(QStringLiteral("启动 HTTP 服务"), card);
    startBtn_->setCursor(Qt::PointingHandCursor);
    startBtn_->setToolTip(QStringLiteral("启动后台 HTTP API 监听"));
    startBtn_->setStyleSheet(QStringLiteral(
        "background: %1; color: %2; border-radius: 6px; padding: 4px 14px; font-weight: bold;")
        .arg(Theme::current().primaryBtn.name(), Theme::current().onPrimary.name()));
    connect(startBtn_, &QPushButton::clicked, this, &ApiPage::startApiServer);
    layout->addWidget(startBtn_);

    stopBtn_ = new QPushButton(QStringLiteral("关闭 HTTP 服务"), card);
    stopBtn_->setCursor(Qt::PointingHandCursor);
    stopBtn_->setToolTip(QStringLiteral("停止后台 HTTP API 监听"));
    stopBtn_->setStyleSheet(QStringLiteral(
        "background: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 4px 14px; font-weight: bold;")
        .arg(Theme::current().dangerBg.name(), Theme::current().danger.name(), Theme::current().dangerStroke.name()));
    connect(stopBtn_, &QPushButton::clicked, this, &ApiPage::stopApiServer);
    layout->addWidget(stopBtn_);

    return card;
}

QWidget *ApiPage::buildPublisherCard()
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("PublisherCard"));
    Theme::decorateCard(card);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);

    auto *title = new QLabel(QStringLiteral("发布服务器"), card);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto *desc = new QLabel(QStringLiteral("用于把本地真实测速成功的节点发布到 Linux Web 后台，不会切换本地数据源。"), card);
    desc->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::current().muted.name()));
    layout->addWidget(desc);

    auto *formRow = new QHBoxLayout();
    formRow->setSpacing(8);

    formRow->addWidget(new QLabel(QStringLiteral("服务器地址:"), card));
    remoteApiEdit_ = new QLineEdit(card);
    remoteApiEdit_->setObjectName(QStringLiteral("PublisherApiEdit"));
    remoteApiEdit_->setPlaceholderText(QStringLiteral("https://你的服务器（默认留空）"));
    remoteApiEdit_->setMinimumWidth(260);
    formRow->addWidget(remoteApiEdit_);

    formRow->addWidget(new QLabel(QStringLiteral("推送 Token:"), card));
    remoteTokenEdit_ = new QLineEdit(card);
    remoteTokenEdit_->setObjectName(QStringLiteral("PublisherTokenEdit"));
    remoteTokenEdit_->setEchoMode(QLineEdit::Password);
    remoteTokenEdit_->setPlaceholderText(QStringLiteral("Bearer Token"));
    remoteTokenEdit_->setMinimumWidth(160);
    formRow->addWidget(remoteTokenEdit_);

    savePublisherBtn_ = new QPushButton(QStringLiteral("保存配置"), card);
    connect(savePublisherBtn_, &QPushButton::clicked, this, &ApiPage::savePublisherConfig);
    formRow->addWidget(savePublisherBtn_);

    clearPublisherBtn_ = new QPushButton(QStringLiteral("清空配置"), card);
    connect(clearPublisherBtn_, &QPushButton::clicked, this, &ApiPage::clearPublisherConfig);
    formRow->addWidget(clearPublisherBtn_);

    checkRemoteBtn_ = new QPushButton(QStringLiteral("检查连接"), card);
    connect(checkRemoteBtn_, &QPushButton::clicked, this, &ApiPage::checkRemoteApi);
    formRow->addWidget(checkRemoteBtn_);

    publishNodesBtn_ = new QPushButton(QStringLiteral("推送可用节点"), card);
    publishNodesBtn_->setStyleSheet(QStringLiteral(
        "background: %1; color: %2; border-radius: 4px; padding: 4px 10px; font-weight: bold;")
        .arg(Theme::current().accentBg.name(), Theme::current().accentStrong.name()));
    connect(publishNodesBtn_, &QPushButton::clicked, this, &ApiPage::publishAvailableNodes);
    formRow->addWidget(publishNodesBtn_);

    layout->addLayout(formRow);

    remoteApiStatusLabel_ = new QLabel(card);
    remoteApiStatusLabel_->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::current().muted.name()));
    layout->addWidget(remoteApiStatusLabel_);

    return card;
}

QWidget *ApiPage::buildEndpointsSection()
{
    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 4, 0, 0);
    layout->setSpacing(6);

    auto *sectionTitle = new QLabel(QStringLiteral("接口列表 (26 个接口)"), container);
    QFont stFont = sectionTitle->font();
    stFont.setBold(true);
    sectionTitle->setFont(stFont);
    layout->addWidget(sectionTitle);

    auto *scroll = new QScrollArea(container);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *scrollContent = new QWidget(scroll);
    endpointsListLayout_ = new QVBoxLayout(scrollContent);
    endpointsListLayout_->setContentsMargins(0, 0, 0, 0);
    endpointsListLayout_->setSpacing(6);

    const auto palette = Theme::current();
    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monoFont.setPointSize(10);

    for (const auto &item : kApiItems) {
        auto *itemCard = new QFrame(scrollContent);
        Theme::decorateCard(itemCard);
        itemCard->setObjectName(QStringLiteral("EndpointItem"));

        auto *itemLayout = new QVBoxLayout(itemCard);
        itemLayout->setContentsMargins(12, 8, 12, 8);
        itemLayout->setSpacing(4);

        auto *topRow = new QHBoxLayout();
        topRow->setSpacing(8);

        // Method badge
        auto *badge = new QLabel(QString::fromLatin1(item.method), itemCard);
        QString bgCol, fgCol;
        if (strcmp(item.method, "GET") == 0) {
            bgCol = palette.accentBg.name();
            fgCol = palette.accentStrong.name();
        } else if (strcmp(item.method, "POST") == 0) {
            bgCol = palette.successBg.name();
            fgCol = palette.success.name();
        } else if (strcmp(item.method, "PUT") == 0) {
            bgCol = palette.warningBg.name();
            fgCol = palette.warning.name();
        } else if (strcmp(item.method, "DELETE") == 0) {
            bgCol = palette.dangerBg.name();
            fgCol = palette.danger.name();
        } else {
            bgCol = palette.panel.name();
            fgCol = palette.muted.name();
        }
        badge->setStyleSheet(QStringLiteral(
            "background: %1; color: %2; border-radius: 4px; padding: 2px 7px; font-weight: bold; font-size: 11px;")
            .arg(bgCol, fgCol));
        badge->setFixedHeight(22);
        topRow->addWidget(badge);

        // Path button (click to copy)
        auto *pathBtn = new QPushButton(QString::fromLatin1(item.path), itemCard);
        pathBtn->setFlat(true);
        pathBtn->setFont(monoFont);
        pathBtn->setCursor(Qt::PointingHandCursor);
        pathBtn->setStyleSheet(QStringLiteral(
            "QPushButton { text-align: left; background: transparent; border: none; color: %1; padding: 0; } "
            "QPushButton:hover { text-decoration: underline; }")
            .arg(palette.accent.name()));
        connect(pathBtn, &QPushButton::clicked, this, [this, path = QString::fromLatin1(item.path), openable = item.openable]() {
            const QString url = apiUrl(path, openable);
            copyToClipboard(url, QStringLiteral("已复制: %1").arg(url));
        });
        topRow->addWidget(pathBtn);

        topRow->addStretch();

        // Open button
        if (item.openable && !QString::fromLatin1(item.path).contains(QLatin1Char('{'))) {
            auto *openBtn = new QPushButton(QStringLiteral("打开"), itemCard);
            openBtn->setFixedHeight(24);
            openBtn->setCursor(Qt::PointingHandCursor);
            connect(openBtn, &QPushButton::clicked, this, [this, path = QString::fromLatin1(item.path)]() {
                const QString url = apiUrl(path, true);
                QDesktopServices::openUrl(QUrl(url));
            });
            topRow->addWidget(openBtn);
        }

        // Copy button
        auto *copyBtn = new QPushButton(QStringLiteral("复制"), itemCard);
        copyBtn->setFixedHeight(24);
        copyBtn->setCursor(Qt::PointingHandCursor);
        connect(copyBtn, &QPushButton::clicked, this, [this, path = QString::fromLatin1(item.path), openable = item.openable]() {
            const QString url = apiUrl(path, openable);
            copyToClipboard(url, QStringLiteral("已复制: %1").arg(url));
        });
        topRow->addWidget(copyBtn);

        itemLayout->addLayout(topRow);

        auto *descLabel = new QLabel(QString::fromUtf8(item.desc), itemCard);
        descLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(palette.muted.name()));
        itemLayout->addWidget(descLabel);

        endpointsListLayout_->addWidget(itemCard);
    }

    endpointsListLayout_->addStretch();
    scroll->setWidget(scrollContent);
    layout->addWidget(scroll, 1);

    return container;
}

QString ApiPage::apiUrl(const QString &path, bool withToken) const
{
    const QString addr = context_.apiServer().listenAddr();
    QString url = QStringLiteral("http://%1%2").arg(addr, path);
    const auto tokenOpt = context_.apiServer().token();
    if (withToken && tokenOpt.has_value() && !tokenOpt->isEmpty()) {
        const QChar sep = url.contains(QLatin1Char('?')) ? QLatin1Char('&') : QLatin1Char('?');
        url += QStringLiteral("%1token=%2").arg(sep, *tokenOpt);
    }
    return url;
}

void ApiPage::refresh()
{
    currentSettings_ = context_.settings();
    remoteApiEdit_->setText(currentSettings_.publisherApi);
    remoteTokenEdit_->setText(currentSettings_.publisherToken);
    updateServerStatusUi();
}

void ApiPage::updateServerStatusUi()
{
    const bool running = context_.apiServer().isRunning();
    const QString addr = context_.apiServer().listenAddr();
    const auto token = context_.apiServer().token();
    const auto palette = Theme::current();

    if (running) {
        statusBadge_->setText(QStringLiteral("服务运行中"));
        statusBadge_->setStyleSheet(QStringLiteral(
            "background: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 3px 8px; font-weight: bold;")
            .arg(palette.successBg.name(), palette.success.name(), palette.success.name()));
        listenAddrLabel_->setText(QStringLiteral("本地监听地址: http://%1").arg(addr));
        listenAddrLabel_->setStyleSheet(QStringLiteral("color: %1;").arg(palette.textPrimary.name()));

        if (token.has_value() && !token->isEmpty()) {
            tokenContainer_->setVisible(true);
            tokenLabel_->setText(*token);
        } else {
            tokenContainer_->setVisible(false);
        }

        startBtn_->setVisible(false);
        stopBtn_->setVisible(true);
    } else {
        statusBadge_->setText(QStringLiteral("已停止运行"));
        statusBadge_->setStyleSheet(QStringLiteral(
            "background: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 3px 8px; font-weight: bold;")
            .arg(palette.panel.name(), palette.muted.name(), palette.border.name()));
        listenAddrLabel_->setText(QStringLiteral("本地 HTTP API 服务当前已停止（端口已释放）"));
        listenAddrLabel_->setStyleSheet(QStringLiteral("color: %1;").arg(palette.muted.name()));

        tokenContainer_->setVisible(false);

        startBtn_->setVisible(true);
        stopBtn_->setVisible(false);
    }
}

void ApiPage::startApiServer()
{
    statusFeedbackLabel_->setText(QStringLiteral("正在启动 HTTP API 服务器..."));
    context_.runAsync([this]() {
        context_.apiServer().start();
        Models::UpdateSettings patch;
        patch.httpServerEnabled = true;
        context_.saveSettings(patch);
    }, [this]() {
        refresh();
        statusFeedbackLabel_->setText(QStringLiteral("HTTP API 服务器已启动"));
    });
}

void ApiPage::stopApiServer()
{
    statusFeedbackLabel_->setText(QStringLiteral("正在停止 HTTP API 服务器..."));
    context_.runAsync([this]() {
        context_.apiServer().stop();
        Models::UpdateSettings patch;
        patch.httpServerEnabled = false;
        context_.saveSettings(patch);
        Platform::trimWorkingSet();
    }, [this]() {
        refresh();
        statusFeedbackLabel_->setText(QStringLiteral("HTTP API 服务器已关闭，端口已释放"));
    });
}

void ApiPage::savePublisherConfig()
{
    const QString addr = normalizeApiBase(remoteApiEdit_->text().trimmed());
    remoteApiEdit_->setText(addr);
    const QString token = remoteTokenEdit_->text().trimmed();

    Models::UpdateSettings patch;
    patch.publisherApi = addr;
    patch.publisherToken = token;
    auto res = context_.saveSettings(patch);
    if (res.isOk()) {
        currentSettings_ = context_.settings();
        remoteApiStatusLabel_->setText(QStringLiteral("发布服务器配置已保存"));
        statusFeedbackLabel_->setText(QStringLiteral("发布配置已更新"));
    } else {
        remoteApiStatusLabel_->setText(QStringLiteral("保存失败: %1").arg(res.error()));
    }
}

void ApiPage::clearPublisherConfig()
{
    remoteApiEdit_->clear();
    remoteTokenEdit_->clear();
    Models::UpdateSettings patch;
    patch.publisherApi = QStringLiteral("");
    patch.publisherToken = QStringLiteral("");
    auto res = context_.saveSettings(patch);
    if (res.isOk()) {
        currentSettings_ = context_.settings();
        remoteApiStatusLabel_->setText(QStringLiteral("发布服务器配置已清空"));
        statusFeedbackLabel_->setText(QStringLiteral("发布配置已清空"));
    }
}

void ApiPage::checkRemoteApi()
{
    QString base = remoteApiEdit_->text().trimmed();
    if (base.isEmpty()) {
        remoteApiStatusLabel_->setText(QStringLiteral("尚未配置发布服务器地址"));
        return;
    }
    base = normalizeApiBase(base);
    const QString token = remoteTokenEdit_->text().trimmed();
    remoteApiStatusLabel_->setText(QStringLiteral("检查中..."));

    auto resultBox = std::make_shared<Result<Net::Response>>(Result<Net::Response>::withValue({}));
    context_.runAsync([base, token, resultBox]() {
        Net::Request req;
        req.url = QStringLiteral("%1/api/push/health").arg(base);
        req.method = QByteArrayLiteral("GET");
        req.timeoutMs = 10000;
        if (!token.isEmpty()) {
            req.headers.push_back(Net::Header{
                QStringLiteral("Authorization"), QStringLiteral("Bearer %1").arg(token)});
        }
        *resultBox = Net::send(req);
    }, [this, resultBox]() {
        if (resultBox->isOk()) {
            if (resultBox->value().status == 200) {
                remoteApiStatusLabel_->setText(QStringLiteral("远程服务在线且 Token 验证成功"));
            } else {
                remoteApiStatusLabel_->setText(
                    QStringLiteral("服务器返回状态码: %1").arg(resultBox->value().status));
            }
        } else {
            remoteApiStatusLabel_->setText(QStringLiteral("连接失败: %1").arg(resultBox->error()));
        }
    });
}

void ApiPage::publishAvailableNodes()
{
    if (busy_) {
        remoteApiStatusLabel_->setText(QStringLiteral("请等待当前任务完成后再推送节点"));
        return;
    }
    QString base = remoteApiEdit_->text().trimmed();
    if (base.isEmpty()) {
        remoteApiStatusLabel_->setText(QStringLiteral("尚未配置发布服务器地址，请先填写"));
        return;
    }
    base = normalizeApiBase(base);
    const QString token = remoteTokenEdit_->text().trimmed();
    if (token.isEmpty()) {
        remoteApiStatusLabel_->setText(QStringLiteral("尚未配置推送 Token，请先填写"));
        return;
    }

    busy_ = true;
    publishNodesBtn_->setEnabled(false);
    remoteApiStatusLabel_->setText(QStringLiteral("正在读取全部真实代理验证成功的节点..."));

    auto dbPtr = context_.databasePtr();
    const QString keywords = context_.settings().excludeKeywords;

    auto resultBox = std::make_shared<Result<QString>>(Result<QString>::withValue(QString()));

    context_.runAsync([dbPtr, base, token, keywords, resultBox]() {
        auto exportRes = DbExport::exportPushableNodes(*dbPtr, 2000, keywords);
        if (exportRes.isErr()) {
            *resultBox = Result<QString>::withError(exportRes.error());
            return;
        }

        const auto &pair = exportRes.value();
        const auto &nodes = pair.first;
        const size_t excludedCount = pair.second;

        if (nodes.isEmpty()) {
            if (excludedCount > 0) {
                *resultBox = Result<QString>::withError(
                    QStringLiteral("验证成功的节点全部来自标记为不推送的源（跳过 %1 个）").arg(excludedCount));
            } else {
                *resultBox = Result<QString>::withError(QStringLiteral("当前没有真实代理验证成功的节点可推送"));
            }
            return;
        }

        nlohmann::json body = nlohmann::json::object();
        nlohmann::json nodesArray = nlohmann::json::array();
        for (const auto &node : nodes) {
            nodesArray.push_back(node.toJson());
        }
        body["nodes"] = std::move(nodesArray);
        const std::string bodyStr = body.dump();

        Net::Request req;
        req.url = QStringLiteral("%1/api/push/nodes").arg(base);
        req.method = QByteArrayLiteral("POST");
        req.contentType = QStringLiteral("application/json");
        req.body = QByteArray::fromStdString(bodyStr);
        req.timeoutMs = 30000;
        req.headers.push_back(Net::Header{
            QStringLiteral("Authorization"), QStringLiteral("Bearer %1").arg(token)});

        auto resp = Net::send(req);
        if (resp.isErr()) {
            *resultBox = Result<QString>::withError(QStringLiteral("连接失败: %1").arg(resp.error()));
            return;
        }

        const auto &response = resp.value();
        if (!response.isSuccess()) {
            *resultBox = Result<QString>::withError(
                QStringLiteral("服务器返回错误: HTTP %1").arg(response.status));
            return;
        }

        try {
            auto jsonResp = nlohmann::json::parse(response.body.toStdString());
            auto data = jsonResp.value("data", nlohmann::json::object());
            qint64 stored = data.value("stored", 0LL);
            qint64 version = data.value("version", 0LL);
            QString skippedNote;
            if (excludedCount > 0) {
                skippedNote = QStringLiteral("，已跳过不推送源 %1 个节点").arg(excludedCount);
            }
            *resultBox = Result<QString>::withValue(
                QStringLiteral("已推送 %1 个节点，服务器保存 %2 个（版本 %3）%4")
                    .arg(nodes.size())
                    .arg(stored)
                    .arg(version)
                    .arg(skippedNote));
        } catch (const std::exception &e) {
            *resultBox = Result<QString>::withError(QStringLiteral("解析服务器响应失败: %1").arg(e.what()));
        }
    }, [this, resultBox]() {
        busy_ = false;
        publishNodesBtn_->setEnabled(true);
        if (resultBox->isOk()) {
            remoteApiStatusLabel_->setText(resultBox->value());
        } else {
            remoteApiStatusLabel_->setText(resultBox->error());
        }
    });
}

void ApiPage::copyToClipboard(const QString &text, const QString &feedback)
{
    if (auto *clip = QGuiApplication::clipboard()) {
        clip->setText(text);
    }
    statusFeedbackLabel_->setText(feedback);
}

QWidget *createApiPage(App::AppContext &context, QWidget *parent)
{
    return new ApiPage(context, parent);
}

} // namespace Gui
