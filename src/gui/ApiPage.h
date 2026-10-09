#pragma once

#include "gui/Page.h"
#include "models/Settings.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
QT_END_NAMESPACE

namespace Gui {

/// 外部 HTTP API 与发布服务器管理页，对应 Rust `gui/views/settings.rs` 的 `ui_api`。
class ApiPage : public Page {
    Q_OBJECT

public:
    explicit ApiPage(App::AppContext &context, QWidget *parent = nullptr);

    void refresh() override;

public slots:
    void startApiServer();
    void stopApiServer();
    void savePublisherConfig();
    void clearPublisherConfig();
    void checkRemoteApi();
    void publishAvailableNodes();
    void copyToClipboard(const QString &text, const QString &feedback);

private:
    QWidget *buildHeader();
    QWidget *buildServerStatusCard();
    QWidget *buildPublisherCard();
    QWidget *buildEndpointsSection();

    QString apiUrl(const QString &path, bool withToken) const;
    void updateServerStatusUi();

    Models::AppSettings currentSettings_;
    bool busy_ = false;

    // 头部反馈
    QLabel *statusFeedbackLabel_ = nullptr;

    // Server Status Card
    QLabel *statusBadge_ = nullptr;
    QLabel *listenAddrLabel_ = nullptr;
    QWidget *tokenContainer_ = nullptr;
    QPushButton *tokenLabel_ = nullptr;
    QPushButton *startBtn_ = nullptr;
    QPushButton *stopBtn_ = nullptr;

    // Publisher Card
    QLineEdit *remoteApiEdit_ = nullptr;
    QLineEdit *remoteTokenEdit_ = nullptr;
    QPushButton *savePublisherBtn_ = nullptr;
    QPushButton *clearPublisherBtn_ = nullptr;
    QPushButton *checkRemoteBtn_ = nullptr;
    QPushButton *publishNodesBtn_ = nullptr;
    QLabel *remoteApiStatusLabel_ = nullptr;

    // Endpoints Section
    QVBoxLayout *endpointsListLayout_ = nullptr;
};

} // namespace Gui
