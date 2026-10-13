#include "gui/IpRiskDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "app/AppContext.h"
#include "gui/Theme.h"
#include "proxy/ProxyManager.h"

namespace Gui {

IpRiskDialog::IpRiskDialog(App::AppContext &context, const QString &nodeId,
                           const QString &nodeName, const QString &server,
                           QWidget *parent)
    : QDialog(parent), context_(context), nodeId_(nodeId),
      nodeName_(nodeName), server_(server)
{
    setWindowTitle(QStringLiteral("IP 检测 · 归属与风控画像"));
    setFixedSize(480, 520);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(12);

    auto *headBox = new QVBoxLayout();
    headBox->setSpacing(4);
    titleLabel_ = new QLabel(QStringLiteral("IP 检测 · 归属与风控画像"), this);
    titleLabel_->setObjectName(QStringLiteral("Title"));
    titleLabel_->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700;"));
    headBox->addWidget(titleLabel_);

    auto *sub = new QLabel(nodeName_.isEmpty() ? server_ : QStringLiteral("%1 (%2)").arg(nodeName_, server_), this);
    sub->setObjectName(QStringLiteral("Subtitle"));
    headBox->addWidget(sub);
    root->addLayout(headBox);

    contentWidget_ = new QWidget(this);
    contentBox_ = new QVBoxLayout(contentWidget_);
    contentBox_->setContentsMargins(0, 0, 0, 0);
    contentBox_->setSpacing(10);
    root->addWidget(contentWidget_, 1);

    auto *actionLine = new QHBoxLayout();
    actionLine->setSpacing(8);

    copyButton_ = new QPushButton(QStringLiteral("复制"), this);
    copyButton_->setEnabled(false);
    connect(copyButton_, &QPushButton::clicked, this, [this]() {
        if (!ingressRisk_.has_value()) {
            return;
        }
        QString text = QStringLiteral("入口：%1\n%2").arg(ingressRisk_->headline(), ingressRisk_->verdict);
        if (egressRisk_.has_value()) {
            text += QStringLiteral("\n出口：%1\n%2").arg(egressRisk_->headline(), egressRisk_->verdict);
        }
        QApplication::clipboard()->setText(text);
        if (closeButton_ != nullptr) {
            closeButton_->setText(QStringLiteral("已复制！关闭"));
        }
    });
    actionLine->addWidget(copyButton_);

    retryButton_ = new QPushButton(QStringLiteral("重新查询"), this);
    connect(retryButton_, &QPushButton::clicked, this, &IpRiskDialog::runCheck);
    actionLine->addWidget(retryButton_);

    actionLine->addStretch();

    closeButton_ = new QPushButton(QStringLiteral("关闭"), this);
    connect(closeButton_, &QPushButton::clicked, this, &QDialog::accept);
    actionLine->addWidget(closeButton_);

    root->addLayout(actionLine);

    runCheck();
}

void IpRiskDialog::runCheck()
{
    if (busy_) {
        return;
    }
    busy_ = true;
    errorMessage_.clear();
    ingressRisk_.reset();
    egressRisk_.reset();
    egressError_.clear();
    copyButton_->setEnabled(false);
    updateUi();

    const QString server = server_;
    const QString targetNodeId = nodeId_;
    const auto proxyStatus = context_.proxy().status();
    const bool isRunningTarget = proxyStatus.localProxyActive() && proxyStatus.nodeId == targetNodeId;
    const quint16 mixedPort = proxyStatus.mixedPort;

    struct CheckResult {
        QString error;
        std::optional<IpRisk::Risk> ingress;
        std::optional<IpRisk::Risk> egress;
        QString egressErr;
    };
    auto resultBox = std::make_shared<CheckResult>();

    context_.runAsync(
        [server, isRunningTarget, mixedPort, resultBox]() {
            // 1. 解析目标 IP
            QString ip = IpRisk::addressText(server);
            if (ip.isEmpty()) {
                const auto resolved = IpRisk::resolveFirst(server);
                if (resolved.isErr()) {
                    resultBox->error = QStringLiteral("解析节点域名失败：%1").arg(resolved.error());
                    return;
                }
                ip = resolved.value();
            }

            // 2. 查询入口画像
            const QString infoUrl = IpRisk::ipinfoUrl(ip);
            const auto infoRes = IpRisk::fetchText(infoUrl, IpRisk::whatIpinfo(), QString());
            if (infoRes.isErr()) {
                resultBox->error = QStringLiteral("查询 ipinfo 失败：%1").arg(infoRes.error());
                return;
            }
            const QString apiUrl = IpRisk::ipApiUrl(ip);
            const auto apiRes = IpRisk::fetchText(apiUrl, IpRisk::whatIpApi(), QString());
            const QString apiBody = apiRes.isOk() ? apiRes.value() : QString();

            const auto parsed = IpRisk::parse(server, ip, infoRes.value(), apiBody);
            if (parsed.isErr()) {
                resultBox->error = QStringLiteral("解析风控画像失败：%1").arg(parsed.error());
                return;
            }
            resultBox->ingress = parsed.value();

            // 3. 如果是当前在跑的节点，经代理查一次真实出口
            if (isRunningTarget) {
                const QString proxyUrl = QStringLiteral("http://127.0.0.1:%1").arg(mixedPort);
                QString egressIp;
                for (const QString &echoUrl : IpRisk::egressEchoUrls()) {
                    const auto echoRes = IpRisk::fetchText(echoUrl, QStringLiteral("egress"), proxyUrl);
                    if (echoRes.isOk()) {
                        const QString candidate = echoRes.value().trimmed();
                        if (!IpRisk::addressText(candidate).isEmpty()) {
                            egressIp = candidate;
                            break;
                        }
                    }
                }
                if (egressIp.isEmpty()) {
                    resultBox->egressErr = QStringLiteral("经当前代理查询出口 IP 超时或失败");
                } else {
                    const auto egInfo = IpRisk::fetchText(IpRisk::ipinfoUrl(egressIp), IpRisk::whatIpinfo(), QString());
                    const auto egApi = IpRisk::fetchText(IpRisk::ipApiUrl(egressIp), IpRisk::whatIpApi(), QString());
                    if (egInfo.isOk()) {
                        const auto egParsed = IpRisk::parse(egressIp, egressIp, egInfo.value(), egApi.isOk() ? egApi.value() : QString());
                        if (egParsed.isOk()) {
                            resultBox->egress = egParsed.value();
                        }
                    }
                }
            }
        },
        [this, resultBox]() {
            busy_ = false;
            errorMessage_ = resultBox->error;
            ingressRisk_ = resultBox->ingress;
            egressRisk_ = resultBox->egress;
            egressError_ = resultBox->egressErr;
            if (ingressRisk_.has_value()) {
                copyButton_->setEnabled(true);
            }
            updateUi();
        }, this);
}

void IpRiskDialog::updateUi()
{
    // 清空现有控件
    while (QLayoutItem *item = contentBox_->takeAt(0)) {
        if (item->widget() != nullptr) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    if (busy_) {
        auto *card = new QFrame(contentWidget_);
        Theme::decorateCard(card);
        auto *cardBox = new QVBoxLayout(card);
        cardBox->setContentsMargins(16, 24, 16, 24);
        cardBox->setSpacing(8);

        auto *loading = new QLabel(QStringLiteral("正在查询落地 IP 归属与风控画像..."), card);
        loading->setObjectName(QStringLiteral("Accent"));
        loading->setAlignment(Qt::AlignCenter);
        cardBox->addWidget(loading);

        auto *hint = new QLabel(QStringLiteral("直连查询 ipinfo.io 与 ip-api.com，双源交叉判定"), card);
        hint->setObjectName(QStringLiteral("Muted"));
        hint->setAlignment(Qt::AlignCenter);
        cardBox->addWidget(hint);

        contentBox_->addWidget(card);
        contentBox_->addStretch();
        return;
    }

    if (!errorMessage_.isEmpty()) {
        auto *errCard = new QFrame(contentWidget_);
        Theme::decorateCard(errCard);
        auto *box = new QVBoxLayout(errCard);
        box->setContentsMargins(16, 16, 16, 16);
        box->setSpacing(6);

        auto *errTitle = new QLabel(QStringLiteral("查询失败"), errCard);
        errTitle->setObjectName(QStringLiteral("Danger"));
        box->addWidget(errTitle);

        auto *errDesc = new QLabel(errorMessage_, errCard);
        errDesc->setWordWrap(true);
        box->addWidget(errDesc);

        auto *hint = new QLabel(QStringLiteral("节点地址是域名时可能解析超时；请检查本地网络后重试。"), errCard);
        hint->setObjectName(QStringLiteral("Muted"));
        hint->setWordWrap(true);
        box->addWidget(hint);

        contentBox_->addWidget(errCard);
        contentBox_->addStretch();
        return;
    }

    if (!ingressRisk_.has_value()) {
        auto *emptyLabel = new QLabel(QStringLiteral("暂无查询结果"), contentWidget_);
        emptyLabel->setObjectName(QStringLiteral("Muted"));
        contentBox_->addWidget(emptyLabel);
        contentBox_->addStretch();
        return;
    }

    const auto &risk = ingressRisk_.value();

    // 1. 入口判定卡片
    auto *ingressCard = new QFrame(contentWidget_);
    Theme::decorateCard(ingressCard);
    auto *cardBox = new QVBoxLayout(ingressCard);
    cardBox->setContentsMargins(14, 12, 14, 12);
    cardBox->setSpacing(6);

    auto *badgeLine = new QHBoxLayout();
    badgeLine->setSpacing(8);

    auto *typeBadge = new QLabel(risk.datacenter ? QStringLiteral("机房 / 云厂商") : QStringLiteral("住宅 / ISP"), ingressCard);
    typeBadge->setObjectName(risk.datacenter ? QStringLiteral("WarningPill") : QStringLiteral("SuccessPill"));
    badgeLine->addWidget(typeBadge);

    auto *headLabel = new QLabel(risk.datacenter ? QStringLiteral("风控敏感 · 易受限") : QStringLiteral("原生住宅 · 风控友好"), ingressCard);
    headLabel->setObjectName(risk.datacenter ? QStringLiteral("Warning") : QStringLiteral("Success"));
    badgeLine->addWidget(headLabel);
    badgeLine->addStretch();
    cardBox->addLayout(badgeLine);

    auto *verdictLabel = new QLabel(risk.verdict, ingressCard);
    verdictLabel->setWordWrap(true);
    cardBox->addWidget(verdictLabel);

    contentBox_->addWidget(ingressCard);

    // 2. 详细信息网格
    auto *detailCard = new QFrame(contentWidget_);
    Theme::decorateCard(detailCard);
    auto *grid = new QGridLayout(detailCard);
    grid->setContentsMargins(14, 12, 14, 12);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(6);

    int row = 0;
    if (risk.hostname != risk.ip) {
        auto *k = new QLabel(QStringLiteral("节点地址:"), detailCard);
        k->setObjectName(QStringLiteral("Muted"));
        grid->addWidget(k, row, 0);
        grid->addWidget(new QLabel(risk.hostname, detailCard), row, 1);
        row++;
    }

    auto *ipK = new QLabel(risk.hostname == risk.ip ? QStringLiteral("落地 IP:") : QStringLiteral("解析到:"), detailCard);
    ipK->setObjectName(QStringLiteral("Muted"));
    grid->addWidget(ipK, row, 0);
    grid->addWidget(new QLabel(risk.ip, detailCard), row, 1);
    row++;

    auto *countryK = new QLabel(QStringLiteral("地区:"), detailCard);
    countryK->setObjectName(QStringLiteral("Muted"));
    grid->addWidget(countryK, row, 0);
    grid->addWidget(new QLabel(risk.country.isEmpty() ? QStringLiteral("未知") : risk.country, detailCard), row, 1);
    row++;

    auto *orgK = new QLabel(QStringLiteral("ASN / 运营商:"), detailCard);
    orgK->setObjectName(QStringLiteral("Muted"));
    grid->addWidget(orgK, row, 0);
    auto *orgVal = new QLabel(risk.org.isEmpty() ? QStringLiteral("无 ASN 信息") : risk.org, detailCard);
    orgVal->setWordWrap(true);
    grid->addWidget(orgVal, row, 1);

    contentBox_->addWidget(detailCard);

    // 3. 出口侧（如果有）
    if (egressRisk_.has_value()) {
        const auto &egress = egressRisk_.value();
        auto *egressCard = new QFrame(contentWidget_);
        Theme::decorateCard(egressCard);
        auto *egBox = new QVBoxLayout(egressCard);
        egBox->setContentsMargins(14, 12, 14, 12);
        egBox->setSpacing(6);

        auto *egBadgeLine = new QHBoxLayout();
        egBadgeLine->setSpacing(8);
        auto *egBadge = new QLabel(QStringLiteral("站点实际看到的出口"), egressCard);
        egBadge->setObjectName(QStringLiteral("Accent"));
        egBadgeLine->addWidget(egBadge);
        egBadgeLine->addStretch();
        egBox->addLayout(egBadgeLine);

        egBox->addWidget(new QLabel(egress.headline(), egressCard));
        egBox->addWidget(new QLabel(egress.verdict, egressCard));

        if (egress.ip != risk.ip) {
            auto *diffWarn = new QLabel(QStringLiteral("入口和出口不是同一个 IP：该节点有中转，风控认的是上面这一行。"), egressCard);
            diffWarn->setObjectName(QStringLiteral("Warning"));
            diffWarn->setWordWrap(true);
            egBox->addWidget(diffWarn);
        }
        contentBox_->addWidget(egressCard);
    } else if (!egressError_.isEmpty()) {
        auto *egErrCard = new QFrame(contentWidget_);
        Theme::decorateCard(egErrCard);
        auto *egErrBox = new QVBoxLayout(egErrCard);
        egErrBox->setContentsMargins(14, 8, 14, 8);
        auto *egErrLabel = new QLabel(QStringLiteral("出口查询说明：%1").arg(egressError_), egErrCard);
        egErrLabel->setObjectName(QStringLiteral("Muted"));
        egErrBox->addWidget(egErrLabel);
        contentBox_->addWidget(egErrCard);
    }

    // 4. 底部说明
    auto *footerNote = new QLabel(
        risk.hostname == risk.ip
            ? QStringLiteral("归类只看 ASN / 公司名关键字，查询走直连、不经过代理。")
            : QStringLiteral("节点为域名时查的是解析到的 IP；域名前置或 CDN 中转的节点此处常为前端入口。"),
        contentWidget_);
    footerNote->setObjectName(QStringLiteral("Muted"));
    footerNote->setStyleSheet(QStringLiteral("font-size: 11px;"));
    footerNote->setWordWrap(true);
    contentBox_->addWidget(footerNote);

    contentBox_->addStretch();
}

}
