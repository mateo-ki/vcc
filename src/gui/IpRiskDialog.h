#pragma once

#include <QDialog>
#include <QString>
#include <memory>
#include <optional>

#include "ip/IpRisk.h"

namespace App {
class AppContext;
}

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QVBoxLayout;
class QWidget;
QT_END_NAMESPACE

namespace Gui {

/// 落地风险弹窗：查询过程和结论都在这里显示。
/// 对应 Rust views/proxy.rs 的 ui_ip_risk_dialog。
class IpRiskDialog : public QDialog {
    Q_OBJECT

public:
    explicit IpRiskDialog(App::AppContext &context, const QString &nodeId,
                          const QString &nodeName, const QString &server,
                          QWidget *parent = nullptr);

    void runCheck();

private:
    void updateUi();

    App::AppContext &context_;
    QString nodeId_;
    QString nodeName_;
    QString server_;

    bool busy_ = false;
    QString errorMessage_;
    std::optional<IpRisk::Risk> ingressRisk_;
    std::optional<IpRisk::Risk> egressRisk_;
    QString egressError_;

    QLabel *titleLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QWidget *contentWidget_ = nullptr;
    QVBoxLayout *contentBox_ = nullptr;
    QPushButton *retryButton_ = nullptr;
    QPushButton *copyButton_ = nullptr;
    QPushButton *closeButton_ = nullptr;
};

}
