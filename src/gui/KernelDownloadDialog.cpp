#include "gui/KernelDownloadDialog.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "gui/Theme.h"

namespace Gui {

KernelDownloadDialog::KernelDownloadDialog(Kernel::Kind kind,
                                           Kernel::Source defaultSource,
                                           bool proxyAvailable,
                                           QWidget *parent)
    : QDialog(parent)
    , kind_(kind)
    , proxyAvailable_(proxyAvailable)
{
    setWindowTitle(QStringLiteral("下载内核 · %1").arg(Kernel::kindDisplayName(kind)));
    setFixedWidth(460);
    setModal(true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(14);

    // 顶部标题
    auto *titleLabel = new QLabel(QStringLiteral("下载方式"), this);
    titleLabel->setObjectName(QStringLiteral("Title"));
    titleLabel->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700;"));
    root->addWidget(titleLabel);

    auto *subLabel = new QLabel(
        QStringLiteral("选择从 GitHub Release 镜像通道下载，或直接导入本地已准备好的 zip 压缩包。"), this);
    subLabel->setObjectName(QStringLiteral("Subtitle"));
    subLabel->setWordWrap(true);
    subLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #64748b; margin-bottom: 4px;"));
    root->addWidget(subLabel);

    // 下载方式卡片区
    auto *optionsCard = new QFrame(this);
    Theme::decorateCard(optionsCard);
    auto *optionsLayout = new QVBoxLayout(optionsCard);
    optionsLayout->setContentsMargins(12, 10, 12, 10);
    optionsLayout->setSpacing(8);

    sourceGroup_ = new QButtonGroup(this);

    const auto &options = Kernel::sourceOptions();
    for (int i = 0; i < options.size(); ++i) {
        const auto &opt = options.at(i);
        const bool isProxy = (opt.source == Kernel::Source::Proxy);
        const bool optionEnabled = !isProxy || proxyAvailable_;

        auto *itemWidget = new QWidget(optionsCard);
        auto *itemLayout = new QVBoxLayout(itemWidget);
        itemLayout->setContentsMargins(4, 4, 4, 4);
        itemLayout->setSpacing(2);

        auto *radio = new QRadioButton(opt.label, itemWidget);
        radio->setEnabled(optionEnabled);
        radio->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 13px;"));
        sourceGroup_->addButton(radio, static_cast<int>(opt.source));
        itemLayout->addWidget(radio);

        QString hintText = opt.hint;
        if (isProxy && !proxyAvailable_) {
            hintText += QStringLiteral("（当前本地代理未运行，不可选）");
        }
        auto *hintLabel = new QLabel(hintText, itemWidget);
        hintLabel->setObjectName(QStringLiteral("Subtitle"));
        hintLabel->setStyleSheet(QStringLiteral("color: #64748b; font-size: 11px; margin-left: 20px;"));
        hintLabel->setWordWrap(true);
        itemLayout->addWidget(hintLabel);

        optionsLayout->addWidget(itemWidget);

        if (opt.source == defaultSource && optionEnabled) {
            radio->setChecked(true);
        }
    }

    if (sourceGroup_->checkedButton() == nullptr) {
        // 默认选中第一个可用项
        for (auto *btn : sourceGroup_->buttons()) {
            if (btn->isEnabled()) {
                btn->setChecked(true);
                break;
            }
        }
    }

    root->addWidget(optionsCard);

    // 本地文件选择区（仅当选「本地导入」时可见）
    localFileContainer_ = new QWidget(this);
    auto *localFileLayout = new QVBoxLayout(localFileContainer_);
    localFileLayout->setContentsMargins(0, 0, 0, 0);
    localFileLayout->setSpacing(6);

    auto *localPrompt = new QLabel(QStringLiteral("本地 zip 压缩包路径:"), localFileContainer_);
    localPrompt->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 12px;"));
    localFileLayout->addWidget(localPrompt);

    auto *fileRow = new QHBoxLayout();
    fileRow->setSpacing(8);

    localPathEdit_ = new QLineEdit(localFileContainer_);
    localPathEdit_->setPlaceholderText(QStringLiteral(R"(D:\downloads\mihomo-windows-amd64.zip)"));
    connect(localPathEdit_, &QLineEdit::textChanged, this, [this]() { updateUiState(); });
    fileRow->addWidget(localPathEdit_, 1);

    browseBtn_ = new QPushButton(QStringLiteral("浏览..."), localFileContainer_);
    browseBtn_->setObjectName(QStringLiteral("Small"));
    browseBtn_->setFixedHeight(30);
    connect(browseBtn_, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("选择内核压缩包"),
            QString(),
            QStringLiteral("Zip 压缩包 (*.zip);;所有文件 (*.*)"));
        if (!path.isEmpty()) {
            localPathEdit_->setText(path);
        }
    });
    fileRow->addWidget(browseBtn_);

    localFileLayout->addLayout(fileRow);
    root->addWidget(localFileContainer_);

    // 错误 / 提示标签
    warningLabel_ = new QLabel(this);
    warningLabel_->setStyleSheet(QStringLiteral("color: #ef4444; font-size: 11px; font-weight: 600;"));
    warningLabel_->setVisible(false);
    root->addWidget(warningLabel_);

    // 底部操作按钮
    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);
    btnRow->addStretch();

    cancelBtn_ = new QPushButton(QStringLiteral("关闭"), this);
    cancelBtn_->setObjectName(QStringLiteral("Small"));
    cancelBtn_->setFixedHeight(32);
    connect(cancelBtn_, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(cancelBtn_);

    startBtn_ = new QPushButton(QStringLiteral("开始下载"), this);
    startBtn_->setObjectName(QStringLiteral("Primary"));
    startBtn_->setFixedHeight(32);
    connect(startBtn_, &QPushButton::clicked, this, [this]() {
        if (selectedSource() == Kernel::Source::Local) {
            const QString path = localFilePath();
            if (path.isEmpty()) {
                warningLabel_->setText(QStringLiteral("请先选好本地 zip 文件"));
                warningLabel_->setVisible(true);
                return;
            }
            if (!QFileInfo::exists(path)) {
                warningLabel_->setText(QStringLiteral("指定的文件不存在，请重新选择"));
                warningLabel_->setVisible(true);
                return;
            }
        }
        accept();
    });
    btnRow->addWidget(startBtn_);

    root->addLayout(btnRow);

    connect(sourceGroup_, &QButtonGroup::idToggled, this, [this]() { updateUiState(); });

    updateUiState();
}

Kernel::Source KernelDownloadDialog::selectedSource() const
{
    const int id = sourceGroup_->checkedId();
    if (id >= 0) {
        return static_cast<Kernel::Source>(id);
    }
    return Kernel::Source::GhProxy;
}

QString KernelDownloadDialog::localFilePath() const
{
    return localPathEdit_ != nullptr ? localPathEdit_->text().trimmed() : QString();
}

void KernelDownloadDialog::updateUiState()
{
    const bool isLocal = (selectedSource() == Kernel::Source::Local);
    localFileContainer_->setVisible(isLocal);

    if (isLocal) {
        startBtn_->setText(QStringLiteral("开始安装"));
        const bool hasPath = !localFilePath().isEmpty();
        if (!hasPath) {
            warningLabel_->setText(QStringLiteral("先选好本地 zip 文件"));
            warningLabel_->setVisible(true);
        } else {
            warningLabel_->setVisible(false);
        }
    } else {
        startBtn_->setText(QStringLiteral("开始下载"));
        warningLabel_->setVisible(false);
    }
}

} // namespace Gui
