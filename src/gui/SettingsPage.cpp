#include "gui/SettingsPage.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include "app/AppContext.h"
#include "db/Database.h"
#include "gui/Pages.h"
#include "gui/Theme.h"
#include "gui/UiText.h"
#include "kernel/KernelManager.h"
#include "net/HttpClient.h"
#include "platform/Firewall.h"
#include "platform/ProcessControl.h"

namespace Gui {

namespace {

QLabel *makeHeaderTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("Title"));
    return label;
}

QFrame *makeCardFrame(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("Card"));
    return card;
}

QString normalizeApiBase(QString raw)
{
    raw = raw.trimmed();
    while (raw.endsWith(QLatin1Char('/'))) {
        raw.chop(1);
    }
    if (!raw.startsWith(QStringLiteral("http://")) && !raw.startsWith(QStringLiteral("https://"))) {
        raw = QStringLiteral("https://") + raw;
    }
    return raw;
}

} // namespace

SettingsPage::SettingsPage(App::AppContext &context, QWidget *parent)
    : Page(context, parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 14, 18, 14);
    root->setSpacing(10);

    root->addWidget(buildHeader());

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(buildScrollContent());
    root->addWidget(scroll, 1);

    root->addWidget(buildBottomBar());

    connect(&context_, &App::AppContext::settingsChanged, this, &SettingsPage::refresh);
    loadFromSettings();
}

QWidget *SettingsPage::buildHeader()
{
    auto *header = new QWidget(this);
    auto *layout = new QVBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto *titleRow = new QHBoxLayout();
    titleRow->addWidget(makeHeaderTitle(QStringLiteral("系统设置"), header));
    auto *sub = new QLabel(QStringLiteral("应用行为 · 定时拉取 · 测速参数 · 网络与自愈服务"), header);
    sub->setStyleSheet(QStringLiteral("color: #888888; font-size: 12px;"));
    titleRow->addWidget(sub);
    titleRow->addStretch();
    layout->addLayout(titleRow);

    auto *actionRow = new QHBoxLayout();
    actionRow->addStretch();

    headerSyncLabel_ = new QLabel(QStringLiteral("✓ 已同步"), header);
    headerSyncLabel_->setStyleSheet(QStringLiteral("color: #10b981; font-size: 11px;"));
    actionRow->addWidget(headerSyncLabel_);

    auto *resetBtn = new QPushButton(QStringLiteral("重置修改"), header);
    resetBtn->setToolTip(QStringLiteral("恢复为当前已保存的配置"));
    connect(resetBtn, &QPushButton::clicked, this, &SettingsPage::resetSettings);
    actionRow->addWidget(resetBtn);

    saveBtn_ = new QPushButton(QStringLiteral("保存设置"), header);
    saveBtn_->setObjectName(QStringLiteral("Primary"));
    connect(saveBtn_, &QPushButton::clicked, this, &SettingsPage::saveSettings);
    actionRow->addWidget(saveBtn_);

    layout->addLayout(actionRow);
    return header;
}

QWidget *SettingsPage::buildScrollContent()
{
    auto *content = new QWidget(this);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    // 高级设置切换复选框
    auto *advRow = new QHBoxLayout();
    advancedToggleCheck_ = new QCheckBox(QStringLiteral("显示高级设置"), content);
    advRow->addWidget(advancedToggleCheck_);
    auto *advHint = new QLabel(QStringLiteral("只留常用的配置；订阅并发、测速深度参数等收在高级设置中"), content);
    advHint->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    advRow->addWidget(advHint);
    advRow->addStretch();
    layout->addLayout(advRow);

    connect(advancedToggleCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        for (auto *w : advancedWidgets_) {
            w->setVisible(checked);
        }
    });

    layout->addWidget(buildCardGeneral());
    layout->addWidget(buildCardSubscription());
    layout->addWidget(buildCardSpeedtest());
    layout->addWidget(buildCardFailover());
    layout->addWidget(buildCardFirewall());
    layout->addWidget(buildCardPublisher());

    for (auto *w : advancedWidgets_) {
        w->setVisible(false);
    }

    layout->addStretch();
    return content;
}

QWidget *SettingsPage::buildCardGeneral()
{
    auto *card = makeCardFrame(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);

    auto *title = new QLabel(QStringLiteral("1. 常规偏好与窗口行为"), card);
    title->setStyleSheet(QStringLiteral("font-weight: 700; font-size: 14px;"));
    layout->addWidget(title);

    auto *desc = new QLabel(QStringLiteral("管理桌面应用程序的关闭逻辑、托盘常驻行为以及系统物理内存回收。"), card);
    desc->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    layout->addWidget(desc);

    autoPrepareKernelCheck_ = new QCheckBox(
        QStringLiteral("预备状态（一个内核都没装）时自动下载 mihomo 内核"), card);
    autoPrepareKernelCheck_->setToolTip(
        QStringLiteral("一次运行只自己尝试一次，关掉它就只在点「下载内核」时才下载。"));
    connect(autoPrepareKernelCheck_, &QCheckBox::toggled, this, &SettingsPage::updateDirtyStatus);
    layout->addWidget(autoPrepareKernelCheck_);

    layout->addSpacing(4);
    auto *closeLabel = new QLabel(QStringLiteral("点击主窗口关闭按钮 (X) 时："), card);
    closeLabel->setStyleSheet(QStringLiteral("font-weight: 600;"));
    layout->addWidget(closeLabel);

    closeToTrayRadio_ = new QRadioButton(QStringLiteral("最小化到系统托盘 (推荐：后台保持代理与定时自动化运行)"), card);
    exitDirectRadio_ = new QRadioButton(QStringLiteral("直接退出程序 (停止所有网络连接并彻底退出软件)"), card);
    connect(closeToTrayRadio_, &QRadioButton::toggled, this, &SettingsPage::updateDirtyStatus);
    layout->addWidget(closeToTrayRadio_);
    layout->addWidget(exitDirectRadio_);

    layout->addSpacing(4);
    auto *themeRow = new QHBoxLayout();
    themeRow->addWidget(new QLabel(QStringLiteral("界面配色主题:"), card));
    themeCombo_ = new QComboBox(card);
    const auto &themeList = Theme::themes();
    for (const auto &t : themeList) {
        themeCombo_->addItem(t.name, t.id);
    }
    connect(themeCombo_, &QComboBox::currentIndexChanged, this, [this]() {
        const QString tid = themeCombo_->currentData().toString();
        Theme::apply(tid);
        updateDirtyStatus();
    });
    themeRow->addWidget(themeCombo_);
    themeRow->addStretch();
    layout->addLayout(themeRow);

    layout->addSpacing(4);
    auto *memRow = new QHBoxLayout();
    trimMemoryBtn_ = new QPushButton(QStringLiteral("立即整理进程内存"), card);
    connect(trimMemoryBtn_, &QPushButton::clicked, this, &SettingsPage::trimMemory);
    memRow->addWidget(trimMemoryBtn_);
    trimStatusLabel_ = new QLabel(card);
    trimStatusLabel_->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    memRow->addWidget(trimStatusLabel_);
    memRow->addStretch();
    layout->addLayout(memRow);

    return card;
}

QWidget *SettingsPage::buildCardSubscription()
{
    auto *card = makeCardFrame(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);

    auto *title = new QLabel(QStringLiteral("2. 订阅定时拉取"), card);
    title->setStyleSheet(QStringLiteral("font-weight: 700; font-size: 14px;"));
    layout->addWidget(title);

    auto *desc = new QLabel(QStringLiteral("在后台周期性拉取已启用的订阅源，自动解析更新节点池。"), card);
    desc->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    layout->addWidget(desc);

    autoFetchCheck_ = new QCheckBox(QStringLiteral("启用后台定时自动拉取全部有效订阅源"), card);
    connect(autoFetchCheck_, &QCheckBox::toggled, this, &SettingsPage::updateDirtyStatus);
    layout->addWidget(autoFetchCheck_);

    auto *form = new QFormLayout();
    form->setSpacing(6);

    autoFetchIntervalSpin_ = new QSpinBox(card);
    autoFetchIntervalSpin_->setRange(5, 1440);
    autoFetchIntervalSpin_->setSuffix(QStringLiteral(" 分钟"));
    connect(autoFetchIntervalSpin_, &QSpinBox::valueChanged, this, &SettingsPage::updateDirtyStatus);
    form->addRow(QStringLiteral("自动拉取周期:"), autoFetchIntervalSpin_);

    excludeKeywordsEdit_ = new QLineEdit(card);
    excludeKeywordsEdit_->setPlaceholderText(QStringLiteral("例如：官网,剩余流量,过期时间（空格/逗号分隔）"));
    connect(excludeKeywordsEdit_, &QLineEdit::textChanged, this, &SettingsPage::updateDirtyStatus);
    form->addRow(QStringLiteral("排除关键词过滤:"), excludeKeywordsEdit_);

    // 高级设置项
    auto *advFetchWidget = new QWidget(card);
    auto *advFetchForm = new QFormLayout(advFetchWidget);
    advFetchForm->setContentsMargins(0, 0, 0, 0);
    advFetchForm->setSpacing(6);

    fetchConcurrencySpin_ = new QSpinBox(advFetchWidget);
    fetchConcurrencySpin_->setRange(1, 16);
    connect(fetchConcurrencySpin_, &QSpinBox::valueChanged, this, &SettingsPage::updateDirtyStatus);
    advFetchForm->addRow(QStringLiteral("订阅拉取并发数:"), fetchConcurrencySpin_);

    fetchTimeoutSpin_ = new QSpinBox(advFetchWidget);
    fetchTimeoutSpin_->setRange(10, 120);
    fetchTimeoutSpin_->setSuffix(QStringLiteral(" 秒"));
    connect(fetchTimeoutSpin_, &QSpinBox::valueChanged, this, &SettingsPage::updateDirtyStatus);
    advFetchForm->addRow(QStringLiteral("单源请求超时:"), fetchTimeoutSpin_);

    advancedWidgets_.push_back(advFetchWidget);
    form->addRow(advFetchWidget);

    layout->addLayout(form);
    return card;
}

QWidget *SettingsPage::buildCardSpeedtest()
{
    auto *card = makeCardFrame(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);

    auto *title = new QLabel(QStringLiteral("3. 节点测速管理与质量检验"), card);
    title->setStyleSheet(QStringLiteral("font-weight: 700; font-size: 14px;"));
    layout->addWidget(title);

    auto *desc = new QLabel(QStringLiteral("配置全协议节点真实握手、TCP 预筛与下载带宽探测策略。"), card);
    desc->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    layout->addWidget(desc);

    autoSpeedtestCheck_ = new QCheckBox(QStringLiteral("订阅拉取完成后自动执行批量测速"), card);
    connect(autoSpeedtestCheck_, &QCheckBox::toggled, this, &SettingsPage::updateDirtyStatus);
    layout->addWidget(autoSpeedtestCheck_);

    speedtestAddedSourceCheck_ = new QCheckBox(QStringLiteral("添加订阅源后自动拉取它，并只测速这个源"), card);
    connect(speedtestAddedSourceCheck_, &QCheckBox::toggled, this, &SettingsPage::updateDirtyStatus);
    layout->addWidget(speedtestAddedSourceCheck_);

    speedtestDownloadCheck_ = new QCheckBox(QStringLiteral("测速时测速下载带宽（会消耗少量流量）"), card);
    connect(speedtestDownloadCheck_, &QCheckBox::toggled, this, &SettingsPage::updateDirtyStatus);
    layout->addWidget(speedtestDownloadCheck_);

    auto *form = new QFormLayout();
    form->setSpacing(6);

    failoverLatencySpin_ = new QSpinBox(card);
    failoverLatencySpin_->setRange(300, 5000);
    failoverLatencySpin_->setSuffix(QStringLiteral(" ms"));
    connect(failoverLatencySpin_, &QSpinBox::valueChanged, this, &SettingsPage::updateDirtyStatus);
    form->addRow(QStringLiteral("故障判定延迟阈值:"), failoverLatencySpin_);

    // 高级设置项
    auto *advSpeedWidget = new QWidget(card);
    auto *advSpeedForm = new QFormLayout(advSpeedWidget);
    advSpeedForm->setContentsMargins(0, 0, 0, 0);
    advSpeedForm->setSpacing(6);

    speedtestConcurrencySpin_ = new QSpinBox(advSpeedWidget);
    speedtestConcurrencySpin_->setRange(1, 512);
    connect(speedtestConcurrencySpin_, &QSpinBox::valueChanged, this, &SettingsPage::updateDirtyStatus);
    advSpeedForm->addRow(QStringLiteral("测速并发上限:"), speedtestConcurrencySpin_);

    speedtestTimeoutSpin_ = new QSpinBox(advSpeedWidget);
    speedtestTimeoutSpin_->setRange(300, 5000);
    speedtestTimeoutSpin_->setSuffix(QStringLiteral(" ms"));
    connect(speedtestTimeoutSpin_, &QSpinBox::valueChanged, this, &SettingsPage::updateDirtyStatus);
    advSpeedForm->addRow(QStringLiteral("单节点测速超时:"), speedtestTimeoutSpin_);

    reuseMinutesSpin_ = new QSpinBox(advSpeedWidget);
    reuseMinutesSpin_->setRange(0, 1440);
    reuseMinutesSpin_->setSuffix(QStringLiteral(" 分钟 (0=不复用)"));
    connect(reuseMinutesSpin_, &QSpinBox::valueChanged, this, &SettingsPage::updateDirtyStatus);
    advSpeedForm->addRow(QStringLiteral("测速结果复用时长:"), reuseMinutesSpin_);

    parallelKernelsCheck_ = new QCheckBox(QStringLiteral("多内核并行验证（峰值内存较高）"), advSpeedWidget);
    connect(parallelKernelsCheck_, &QCheckBox::toggled, this, &SettingsPage::updateDirtyStatus);
    advSpeedForm->addRow(QString(), parallelKernelsCheck_);

    persistentKernelCheck_ = new QCheckBox(QStringLiteral("复用常驻内核（减少内核启动开销）"), advSpeedWidget);
    connect(persistentKernelCheck_, &QCheckBox::toggled, this, &SettingsPage::updateDirtyStatus);
    advSpeedForm->addRow(QString(), persistentKernelCheck_);

    conditionalIsolationCheck_ = new QCheckBox(QStringLiteral("条件隔离复测（仅复测可疑节点）"), advSpeedWidget);
    connect(conditionalIsolationCheck_, &QCheckBox::toggled, this, &SettingsPage::updateDirtyStatus);
    advSpeedForm->addRow(QString(), conditionalIsolationCheck_);

    downloadTopNSpin_ = new QSpinBox(advSpeedWidget);
    downloadTopNSpin_->setRange(0, 500);
    downloadTopNSpin_->setSuffix(QStringLiteral(" 个 (0=全部测)"));
    connect(downloadTopNSpin_, &QSpinBox::valueChanged, this, &SettingsPage::updateDirtyStatus);
    advSpeedForm->addRow(QStringLiteral("下载测速限制前 N 名:"), downloadTopNSpin_);

    advancedWidgets_.push_back(advSpeedWidget);
    form->addRow(advSpeedWidget);

    layout->addLayout(form);
    return card;
}

QWidget *SettingsPage::buildCardFailover()
{
    auto *card = makeCardFrame(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);

    auto *title = new QLabel(QStringLiteral("4. 代理自愈与选路调度"), card);
    title->setStyleSheet(QStringLiteral("font-weight: 700; font-size: 14px;"));
    layout->addWidget(title);

    auto *desc = new QLabel(QStringLiteral("管理断连自动切换 (Failover)、带宽保护与选路调度判定。"), card);
    desc->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    layout->addWidget(desc);

    autoFailoverCheck_ = new QCheckBox(
        QStringLiteral("节点故障自动切换（Failover）：每 60 秒经当前代理实测一次，交给选路调度器判定"), card);
    autoFailoverCheck_->setToolTip(
        QStringLiteral("调度器规则：EWMA 平滑延迟；候选比当前好 25% 以上才切（滞后）；两次切换至少隔 2 分钟。"));
    connect(autoFailoverCheck_, &QCheckBox::toggled, this, &SettingsPage::updateDirtyStatus);
    layout->addWidget(autoFailoverCheck_);

    speedtestAutoRefillCheck_ = new QCheckBox(
        QStringLiteral("测通节点过少时自动全量测速补位"), card);
    connect(speedtestAutoRefillCheck_, &QCheckBox::toggled, this, &SettingsPage::updateDirtyStatus);
    layout->addWidget(speedtestAutoRefillCheck_);

    auto *bwRow = new QHBoxLayout();
    bandwidthGuardCheck_ = new QCheckBox(QStringLiteral("启用带宽保护（持续低于阈值时触发自动切换）"), card);
    connect(bandwidthGuardCheck_, &QCheckBox::toggled, this, &SettingsPage::updateDirtyStatus);
    bwRow->addWidget(bandwidthGuardCheck_);

    bandwidthMinSpin_ = new QSpinBox(card);
    bandwidthMinSpin_->setRange(0, 100000);
    bandwidthMinSpin_->setSuffix(QStringLiteral(" KB/s"));
    connect(bandwidthMinSpin_, &QSpinBox::valueChanged, this, &SettingsPage::updateDirtyStatus);
    bwRow->addWidget(bandwidthMinSpin_);
    bwRow->addStretch();
    layout->addLayout(bwRow);

    return card;
}

QWidget *SettingsPage::buildCardFirewall()
{
    auto *card = makeCardFrame(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);

    auto *title = new QLabel(QStringLiteral("5. Windows 防火墙放行"), card);
    title->setStyleSheet(QStringLiteral("font-weight: 700; font-size: 14px;"));
    layout->addWidget(title);

    auto *desc = new QLabel(
        QStringLiteral("防火墙会拦掉发到本程序与内核监听端口的回环连接。规则按程序路径放行，重编译或换版本后路径不变，规则照样有效。"),
        card);
    desc->setWordWrap(true);
    desc->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    layout->addWidget(desc);

    firewallAutoAllowCheck_ = new QCheckBox(QStringLiteral("软件启动时自动加入允许应用"), card);
    connect(firewallAutoAllowCheck_, &QCheckBox::toggled, this, &SettingsPage::updateDirtyStatus);
    layout->addWidget(firewallAutoAllowCheck_);

    const bool elevated = Platform::isElevated();
    firewallPrivilegeLabel_ = new QLabel(
        elevated ? QStringLiteral("当前是管理员权限，可以立即写入规则")
                 : QStringLiteral("当前非管理员：勾选只记住，提权启动时再自动补上"), card);
    firewallPrivilegeLabel_->setStyleSheet(
        elevated ? QStringLiteral("color: #10b981; font-size: 11px;")
                 : QStringLiteral("color: #f59e0b; font-size: 11px;"));
    layout->addWidget(firewallPrivilegeLabel_);

    auto *btnRow = new QHBoxLayout();
    applyFirewallBtn_ = new QPushButton(QStringLiteral("现在加入"), card);
    applyFirewallBtn_->setEnabled(elevated);
    connect(applyFirewallBtn_, &QPushButton::clicked, this, &SettingsPage::applyFirewallRules);
    btnRow->addWidget(applyFirewallBtn_);

    firewallStatusLabel_ = new QLabel(card);
    firewallStatusLabel_->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    btnRow->addWidget(firewallStatusLabel_);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    return card;
}

QWidget *SettingsPage::buildCardPublisher()
{
    auto *card = makeCardFrame(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);

    auto *title = new QLabel(QStringLiteral("6. 发布服务器"), card);
    title->setStyleSheet(QStringLiteral("font-weight: 700; font-size: 14px;"));
    layout->addWidget(title);

    auto *desc = new QLabel(
        QStringLiteral("用于把本地真实测速成功的节点发布到 Linux Web 后台，不会切换本地数据源。"), card);
    desc->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    layout->addWidget(desc);

    auto *form = new QFormLayout();
    form->setSpacing(6);

    remoteApiEdit_ = new QLineEdit(card);
    remoteApiEdit_->setPlaceholderText(QStringLiteral("https://你的服务器（默认留空）"));
    connect(remoteApiEdit_, &QLineEdit::textChanged, this, &SettingsPage::updateDirtyStatus);
    form->addRow(QStringLiteral("服务器地址:"), remoteApiEdit_);

    remoteTokenEdit_ = new QLineEdit(card);
    remoteTokenEdit_->setEchoMode(QLineEdit::Password);
    remoteTokenEdit_->setPlaceholderText(QStringLiteral("Bearer Token"));
    connect(remoteTokenEdit_, &QLineEdit::textChanged, this, &SettingsPage::updateDirtyStatus);
    form->addRow(QStringLiteral("推送 Token:"), remoteTokenEdit_);

    layout->addLayout(form);

    auto *btnRow = new QHBoxLayout();
    savePublisherBtn_ = new QPushButton(QStringLiteral("保存配置"), card);
    connect(savePublisherBtn_, &QPushButton::clicked, this, &SettingsPage::savePublisherConfig);
    btnRow->addWidget(savePublisherBtn_);

    clearPublisherBtn_ = new QPushButton(QStringLiteral("清空配置"), card);
    connect(clearPublisherBtn_, &QPushButton::clicked, this, &SettingsPage::clearPublisherConfig);
    btnRow->addWidget(clearPublisherBtn_);

    checkRemoteBtn_ = new QPushButton(QStringLiteral("检查连接"), card);
    connect(checkRemoteBtn_, &QPushButton::clicked, this, &SettingsPage::checkRemoteApi);
    btnRow->addWidget(checkRemoteBtn_);

    remoteApiStatusLabel_ = new QLabel(card);
    remoteApiStatusLabel_->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    btnRow->addWidget(remoteApiStatusLabel_);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    return card;
}

QWidget *SettingsPage::buildBottomBar()
{
    auto *bar = new QWidget(this);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 4, 0, 0);
    layout->setSpacing(10);

    bottomSaveBtn_ = new QPushButton(QStringLiteral("保存所有设置"), bar);
    bottomSaveBtn_->setObjectName(QStringLiteral("Primary"));
    connect(bottomSaveBtn_, &QPushButton::clicked, this, &SettingsPage::saveSettings);
    layout->addWidget(bottomSaveBtn_);

    auto *reloadBtn = new QPushButton(QStringLiteral("重新加载"), bar);
    connect(reloadBtn, &QPushButton::clicked, this, &SettingsPage::resetSettings);
    layout->addWidget(reloadBtn);

    bottomSyncLabel_ = new QLabel(QStringLiteral("✓ 设置已保存并同步"), bar);
    bottomSyncLabel_->setStyleSheet(QStringLiteral("color: #10b981; font-size: 11px;"));
    layout->addWidget(bottomSyncLabel_);

    layout->addStretch();
    return bar;
}

void SettingsPage::refresh()
{
    loadFromSettings();
}

void SettingsPage::loadFromSettings()
{
    currentSettings_ = context_.settings();

    autoPrepareKernelCheck_->setChecked(currentSettings_.autoPrepareKernel);
    if (currentSettings_.closeToTray) {
        closeToTrayRadio_->setChecked(true);
    } else {
        exitDirectRadio_->setChecked(true);
    }

    const QString resolvedTheme = Theme::themeById(currentSettings_.uiTheme).id;
    const int tidx = themeCombo_->findData(resolvedTheme);
    if (tidx >= 0) {
        themeCombo_->setCurrentIndex(tidx);
    }

    autoFetchCheck_->setChecked(currentSettings_.autoFetchEnabled);
    autoFetchIntervalSpin_->setValue(currentSettings_.autoFetchIntervalMin);
    fetchConcurrencySpin_->setValue(currentSettings_.fetchConcurrency);
    fetchTimeoutSpin_->setValue(currentSettings_.fetchTimeoutSecs);
    excludeKeywordsEdit_->setText(currentSettings_.excludeKeywords);

    autoSpeedtestCheck_->setChecked(currentSettings_.autoSpeedtestEnabled);
    speedtestAddedSourceCheck_->setChecked(currentSettings_.speedtestAddedSource);
    speedtestDownloadCheck_->setChecked(currentSettings_.speedtestDownload);
    failoverLatencySpin_->setValue(currentSettings_.failoverLatencyMs);
    speedtestConcurrencySpin_->setValue(currentSettings_.speedtestConcurrency);
    speedtestTimeoutSpin_->setValue(currentSettings_.speedtestTimeoutMs);
    reuseMinutesSpin_->setValue(currentSettings_.speedtestReuseMinutes);
    parallelKernelsCheck_->setChecked(currentSettings_.speedtestParallelKernels);
    persistentKernelCheck_->setChecked(currentSettings_.speedtestPersistentKernel);
    conditionalIsolationCheck_->setChecked(currentSettings_.speedtestConditionalIsolation);
    downloadTopNSpin_->setValue(currentSettings_.speedtestDownloadTopN);

    autoFailoverCheck_->setChecked(currentSettings_.autoFailover);
    speedtestAutoRefillCheck_->setChecked(currentSettings_.speedtestAutoRefill);
    bandwidthGuardCheck_->setChecked(currentSettings_.bandwidthGuard);
    bandwidthMinSpin_->setValue(currentSettings_.bandwidthMinKbps);

    firewallAutoAllowCheck_->setChecked(currentSettings_.firewallAutoAllow);

    remoteApiEdit_->setText(currentSettings_.publisherApi);
    remoteTokenEdit_->setText(currentSettings_.publisherToken);

    updateDirtyStatus();
}

bool SettingsPage::isDirty() const
{
    if (autoPrepareKernelCheck_->isChecked() != currentSettings_.autoPrepareKernel) return true;
    if (closeToTrayRadio_->isChecked() != currentSettings_.closeToTray) return true;
    const QString resolvedTheme = Theme::themeById(currentSettings_.uiTheme).id;
    if (themeCombo_->currentData().toString() != resolvedTheme) return true;

    if (autoFetchCheck_->isChecked() != currentSettings_.autoFetchEnabled) return true;
    if (autoFetchIntervalSpin_->value() != currentSettings_.autoFetchIntervalMin) return true;
    if (fetchConcurrencySpin_->value() != currentSettings_.fetchConcurrency) return true;
    if (fetchTimeoutSpin_->value() != currentSettings_.fetchTimeoutSecs) return true;
    if (excludeKeywordsEdit_->text().trimmed() != currentSettings_.excludeKeywords.trimmed()) return true;

    if (autoSpeedtestCheck_->isChecked() != currentSettings_.autoSpeedtestEnabled) return true;
    if (speedtestAddedSourceCheck_->isChecked() != currentSettings_.speedtestAddedSource) return true;
    if (speedtestDownloadCheck_->isChecked() != currentSettings_.speedtestDownload) return true;
    if (failoverLatencySpin_->value() != currentSettings_.failoverLatencyMs) return true;
    if (speedtestConcurrencySpin_->value() != currentSettings_.speedtestConcurrency) return true;
    if (speedtestTimeoutSpin_->value() != currentSettings_.speedtestTimeoutMs) return true;
    if (reuseMinutesSpin_->value() != currentSettings_.speedtestReuseMinutes) return true;
    if (parallelKernelsCheck_->isChecked() != currentSettings_.speedtestParallelKernels) return true;
    if (persistentKernelCheck_->isChecked() != currentSettings_.speedtestPersistentKernel) return true;
    if (conditionalIsolationCheck_->isChecked() != currentSettings_.speedtestConditionalIsolation) return true;
    if (downloadTopNSpin_->value() != currentSettings_.speedtestDownloadTopN) return true;

    if (autoFailoverCheck_->isChecked() != currentSettings_.autoFailover) return true;
    if (speedtestAutoRefillCheck_->isChecked() != currentSettings_.speedtestAutoRefill) return true;
    if (bandwidthGuardCheck_->isChecked() != currentSettings_.bandwidthGuard) return true;
    if (bandwidthMinSpin_->value() != currentSettings_.bandwidthMinKbps) return true;

    if (firewallAutoAllowCheck_->isChecked() != currentSettings_.firewallAutoAllow) return true;

    if (remoteApiEdit_->text().trimmed() != currentSettings_.publisherApi.trimmed()) return true;
    if (remoteTokenEdit_->text().trimmed() != currentSettings_.publisherToken.trimmed()) return true;

    return false;
}

void SettingsPage::updateDirtyStatus()
{
    const bool dirty = isDirty();
    const QString text = dirty ? QStringLiteral("⚠ 有未保存修改") : QStringLiteral("✓ 已同步");
    const QString style = dirty ? QStringLiteral("color: #f59e0b; font-size: 11px;")
                                : QStringLiteral("color: #10b981; font-size: 11px;");

    headerSyncLabel_->setText(text);
    headerSyncLabel_->setStyleSheet(style);
    bottomSyncLabel_->setText(dirty ? QStringLiteral("⚠ 有未保存修改（建议点击保存）") : QStringLiteral("✓ 设置已保存并同步"));
    bottomSyncLabel_->setStyleSheet(style);
}

void SettingsPage::saveSettings()
{
    if (saving_) return;
    saving_ = true;

    Models::UpdateSettings patch;
    patch.autoPrepareKernel = autoPrepareKernelCheck_->isChecked();
    patch.closeToTray = closeToTrayRadio_->isChecked();
    patch.uiTheme = themeCombo_->currentData().toString();

    patch.autoFetchEnabled = autoFetchCheck_->isChecked();
    patch.autoFetchIntervalMin = autoFetchIntervalSpin_->value();
    patch.fetchConcurrency = fetchConcurrencySpin_->value();
    patch.fetchTimeoutSecs = fetchTimeoutSpin_->value();
    patch.excludeKeywords = excludeKeywordsEdit_->text().trimmed();

    patch.autoSpeedtestEnabled = autoSpeedtestCheck_->isChecked();
    patch.speedtestAddedSource = speedtestAddedSourceCheck_->isChecked();
    patch.speedtestDownload = speedtestDownloadCheck_->isChecked();
    patch.failoverLatencyMs = failoverLatencySpin_->value();
    patch.speedtestConcurrency = speedtestConcurrencySpin_->value();
    patch.speedtestTimeoutMs = speedtestTimeoutSpin_->value();
    patch.speedtestReuseMinutes = reuseMinutesSpin_->value();
    patch.speedtestParallelKernels = parallelKernelsCheck_->isChecked();
    patch.speedtestPersistentKernel = persistentKernelCheck_->isChecked();
    patch.speedtestConditionalIsolation = conditionalIsolationCheck_->isChecked();
    patch.speedtestDownloadTopN = downloadTopNSpin_->value();

    patch.autoFailover = autoFailoverCheck_->isChecked();
    patch.speedtestAutoRefill = speedtestAutoRefillCheck_->isChecked();
    patch.bandwidthGuard = bandwidthGuardCheck_->isChecked();
    patch.bandwidthMinKbps = bandwidthMinSpin_->value();

    patch.firewallAutoAllow = firewallAutoAllowCheck_->isChecked();

    patch.publisherApi = remoteApiEdit_->text().trimmed();
    patch.publisherToken = remoteTokenEdit_->text().trimmed();

    auto res = context_.saveSettings(patch);
    saving_ = false;
    if (res.isOk()) {
        currentSettings_ = res.value();
        updateDirtyStatus();
        bottomSyncLabel_->setText(QStringLiteral("✓ 设置已成功保存"));
    } else {
        QMessageBox::critical(this, QStringLiteral("保存失败"), res.error());
    }
}

void SettingsPage::resetSettings()
{
    loadFromSettings();
}

void SettingsPage::trimMemory()
{
    Platform::trimWorkingSet();
    trimStatusLabel_->setText(QStringLiteral("工作集内存已修剪释放"));
}

void SettingsPage::applyFirewallRules()
{
    QStringList paths;
    paths << QCoreApplication::applicationFilePath();
    for (Kernel::Kind kind : {Kernel::Kind::Mihomo, Kernel::Kind::SingBox, Kernel::Kind::Xray}) {
        auto p = Kernel::findKernelExecutable(kind);
        if (p.has_value()) {
            paths << p.value();
        }
    }
    auto res = Platform::Firewall::ensureAll(paths);
    if (res.isOk()) {
        firewallStatusLabel_->setText(res.value());
    } else {
        firewallStatusLabel_->setText(QStringLiteral("设置规则失败: %1").arg(res.error()));
    }
}

void SettingsPage::savePublisherConfig()
{
    const QString addr = remoteApiEdit_->text().trimmed().isEmpty()
                             ? QString()
                             : normalizeApiBase(remoteApiEdit_->text().trimmed());
    remoteApiEdit_->setText(addr);
    const QString token = remoteTokenEdit_->text().trimmed();

    auto res = context_.database().updatePublisher(addr, token);
    if (res.isOk()) {
        context_.reloadSettings();
        currentSettings_ = context_.settings();
        remoteApiStatusLabel_->setText(QStringLiteral("发布服务器配置已更新"));
        updateDirtyStatus();
    } else {
        remoteApiStatusLabel_->setText(QStringLiteral("保存失败: %1").arg(res.error()));
    }
}

void SettingsPage::clearPublisherConfig()
{
    remoteApiEdit_->clear();
    remoteTokenEdit_->clear();
    auto res = context_.database().updatePublisher(QString(), QString());
    if (res.isOk()) {
        context_.reloadSettings();
        currentSettings_ = context_.settings();
        remoteApiStatusLabel_->setText(QStringLiteral("发布服务器配置已清空"));
        updateDirtyStatus();
    }
}

void SettingsPage::checkRemoteApi()
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

QWidget *createSettingsPage(App::AppContext &context, QWidget *parent)
{
    return new SettingsPage(context, parent);
}

} // namespace Gui
