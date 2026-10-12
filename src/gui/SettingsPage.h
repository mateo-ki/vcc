#pragma once

#include "gui/Page.h"
#include "models/Settings.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
QT_END_NAMESPACE

namespace Gui {

/// 系统设置管理页，对应 Rust `gui/views/settings.rs` 的 `ui_settings`。
class SettingsPage : public Page {
    Q_OBJECT

public:
    explicit SettingsPage(App::AppContext &context, QWidget *parent = nullptr);

    void refresh() override;

    bool isDirty() const;

public slots:
    void saveSettings();
    void resetSettings();
    void trimMemory();
    void applyFirewallRules();
    void checkRemoteApi();
    void savePublisherConfig();
    void clearPublisherConfig();

private:
    QWidget *buildHeader();
    QWidget *buildScrollContent();
    QWidget *buildCardGeneral();
    QWidget *buildCardSubscription();
    QWidget *buildCardSpeedtest();
    QWidget *buildCardFailover();
    QWidget *buildCardFirewall();
    QWidget *buildCardPublisher();
    QWidget *buildBottomBar();

    void loadFromSettings();
    void updateDirtyStatus();

    // 状态
    Models::AppSettings currentSettings_;
    bool saving_ = false;

    // 顶部与底部同步状态
    QLabel *headerSyncLabel_ = nullptr;
    QLabel *bottomSyncLabel_ = nullptr;
    QPushButton *saveBtn_ = nullptr;
    QPushButton *bottomSaveBtn_ = nullptr;

    // 高级设置显隐
    QCheckBox *advancedToggleCheck_ = nullptr;
    QVector<QWidget *> advancedWidgets_;

    // Card 1: 常规
    QCheckBox *autoPrepareKernelCheck_ = nullptr;
    QRadioButton *closeToTrayRadio_ = nullptr;
    QRadioButton *exitDirectRadio_ = nullptr;
    QComboBox *themeCombo_ = nullptr;
    QPushButton *trimMemoryBtn_ = nullptr;
    QLabel *trimStatusLabel_ = nullptr;

    // Card 2: 订阅自动拉取
    QCheckBox *autoFetchCheck_ = nullptr;
    QSpinBox *autoFetchIntervalSpin_ = nullptr;
    QSpinBox *fetchConcurrencySpin_ = nullptr;
    QSpinBox *fetchTimeoutSpin_ = nullptr;
    QLineEdit *excludeKeywordsEdit_ = nullptr;

    // Card 3: 节点测速
    QCheckBox *autoSpeedtestCheck_ = nullptr;
    QCheckBox *speedtestAddedSourceCheck_ = nullptr;
    QCheckBox *speedtestDownloadCheck_ = nullptr;
    QSpinBox *failoverLatencySpin_ = nullptr;
    QSpinBox *speedtestConcurrencySpin_ = nullptr;
    QSpinBox *speedtestTimeoutSpin_ = nullptr;
    QSpinBox *reuseMinutesSpin_ = nullptr;
    QCheckBox *parallelKernelsCheck_ = nullptr;
    QCheckBox *persistentKernelCheck_ = nullptr;
    QCheckBox *conditionalIsolationCheck_ = nullptr;
    QSpinBox *downloadTopNSpin_ = nullptr;

    // Card 4: 代理自愈
    QPlainTextEdit *failoverProbeUrlsEdit_ = nullptr;
    QCheckBox *autoFailoverCheck_ = nullptr;
    QCheckBox *speedtestAutoRefillCheck_ = nullptr;
    QCheckBox *bandwidthGuardCheck_ = nullptr;
    QSpinBox *bandwidthMinSpin_ = nullptr;

    // Card 5: 防火墙
    QCheckBox *firewallAutoAllowCheck_ = nullptr;
    QLabel *firewallPrivilegeLabel_ = nullptr;
    QPushButton *applyFirewallBtn_ = nullptr;
    QLabel *firewallStatusLabel_ = nullptr;

    // Card 6: 发布服务器
    QLineEdit *remoteApiEdit_ = nullptr;
    QLineEdit *remoteTokenEdit_ = nullptr;
    QPushButton *savePublisherBtn_ = nullptr;
    QPushButton *clearPublisherBtn_ = nullptr;
    QPushButton *checkRemoteBtn_ = nullptr;
    QLabel *remoteApiStatusLabel_ = nullptr;
};

} // namespace Gui
