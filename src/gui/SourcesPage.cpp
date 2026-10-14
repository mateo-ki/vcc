#include "gui/SourcesPage.h"
#include "gui/ProgressThrottle.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QCoreApplication>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "app/AppContext.h"
#include "collector/Collector.h"
#include "collector/GistDiscovery.h"
#include "collector/Telegram.h"
#include "core/Log.h"
#include "db/Database.h"
#include "db/DatabaseTelemetry.h"
#include "gui/Pages.h"
#include "gui/Theme.h"
#include "gui/UiText.h"
#include "models/Source.h"
#include "parser/Subscription.h"
#include "proxy/ProxyManager.h"

namespace Gui {

namespace {

QLabel *makeHeaderTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("Title"));
    return label;
}

/// 订阅源添加/编辑对话框
class SourceEditDialog : public QDialog {
public:
    SourceEditDialog(const Models::Source *existing, QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(existing != nullptr ? QStringLiteral("编辑订阅源") : QStringLiteral("新增订阅源"));
        setMinimumWidth(520);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(18, 16, 18, 16);
        root->setSpacing(12);

        auto *form = new QFormLayout();
        form->setLabelAlignment(Qt::AlignRight);
        form->setSpacing(10);

        nameEdit_ = new QLineEdit(this);
        nameEdit_->setPlaceholderText(QStringLiteral("订阅源名称"));
        if (existing != nullptr) {
            nameEdit_->setText(existing->name);
        }
        form->addRow(QStringLiteral("名称:"), nameEdit_);

        urlEdit_ = new QPlainTextEdit(this);
        urlEdit_->setPlaceholderText(QStringLiteral("订阅 URL 模板，支持 https:// 或 Telegram 频道链接"));
        urlEdit_->setFixedHeight(75);
        if (existing != nullptr) {
            urlEdit_->setPlainText(existing->urlTemplate);
        }
        // 标签右边挂一个 ?：把展开规则摆在填表的地方，而不是等人去翻文档。
        // 清单必须和 Parser::expandUrlTemplate 认的那 14 个一一对得上（Common.cpp）。
        auto *urlLabelRow = new QWidget(this);
        auto *urlLabelBox = new QHBoxLayout(urlLabelRow);
        urlLabelBox->setContentsMargins(0, 0, 0, 0);
        urlLabelBox->setSpacing(4);
        urlLabelBox->addWidget(new QLabel(QStringLiteral("URL 模板:"), urlLabelRow));
        auto *urlHelp = new QLabel(QStringLiteral("?"), urlLabelRow);
        urlHelp->setToolTip(SourcesPage::urlTemplateHelp());
        Gui::Theme::setWidgetCss(urlHelp, QStringLiteral("color: {muted}; font-weight: 700;"));
        urlLabelBox->addWidget(urlHelp);
        urlLabelBox->addStretch();
        form->addRow(urlLabelRow, urlEdit_);

        telegramHint_ = new QLabel(this);
        telegramHint_->setWordWrap(true);
        Gui::Theme::setWidgetCss(telegramHint_, QStringLiteral("color: {success}; font-size: 11px;"));
        telegramHint_->setVisible(false);
        form->addRow(QString(), telegramHint_);

        enabledCheck_ = new QCheckBox(QStringLiteral("启用该订阅源（参与批量自动拉取）"), this);
        enabledCheck_->setChecked(existing != nullptr ? existing->enabled : true);
        form->addRow(QStringLiteral("状态:"), enabledCheck_);

        remarkEdit_ = new QLineEdit(this);
        remarkEdit_->setPlaceholderText(QStringLiteral("可选备注"));
        if (existing != nullptr && !existing->remark.isEmpty()) {
            remarkEdit_->setText(existing->remark);
        }
        form->addRow(QStringLiteral("备注:"), remarkEdit_);

        root->addLayout(form);

        auto checkTelegram = [this]() {
            const QString rawUrl = urlEdit_->toPlainText().trimmed();
            const QString channel = Collector::Telegram::channelName(rawUrl);
            if (!channel.isEmpty()) {
                telegramHint_->setText(
                    QStringLiteral("Telegram 频道 · @%1 —— 读公开网页预览；节点只增不删，旧节点交给测速淘汰。t.me 直连不通，拉取时需挂代理。")
                        .arg(channel));
                telegramHint_->setVisible(true);
            } else {
                telegramHint_->setVisible(false);
            }
        };
        connect(urlEdit_, &QPlainTextEdit::textChanged, this, checkTelegram);
        checkTelegram();

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("Primary"));
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            QString nameStr = nameEdit_->text().trimmed();
            QString urlStr = urlEdit_->toPlainText().trimmed();
            if (nameStr.isEmpty() || urlStr.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("名称和 URL 不能为空"));
                return;
            }
            // 智能容错：用户把 URL 粘到了名称框，把名称填到了 URL 框
            const bool nameLooksLikeUrl = nameStr.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
                || nameStr.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
            const bool urlLooksLikeUrl = urlStr.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
                || urlStr.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)
                || urlStr.startsWith(QStringLiteral("t.me/"), Qt::CaseInsensitive)
                || urlStr.startsWith(QStringLiteral("@"));
            if (nameLooksLikeUrl && !urlLooksLikeUrl) {
                std::swap(nameStr, urlStr);
                nameEdit_->setText(nameStr);
                urlEdit_->setPlainText(urlStr);
            }
            if (!urlStr.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
                && !urlStr.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)
                && !urlStr.startsWith(QStringLiteral("t.me/"), Qt::CaseInsensitive)
                && !urlStr.startsWith(QStringLiteral("@"))) {
                QMessageBox::warning(this, QStringLiteral("提示"),
                    QStringLiteral("URL 格式不正确，必须以 http:// 或 https:// 或 t.me/ 开头"));
                return;
            }
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        root->addWidget(buttons);
    }

    QString name() const { return nameEdit_->text().trimmed(); }
    QString url() const { return urlEdit_->toPlainText().trimmed(); }
    bool isEnabled() const { return enabledCheck_->isChecked(); }
    QString remark() const { return remarkEdit_->text().trimmed(); }

    void setName(const QString &n) { if (nameEdit_) nameEdit_->setText(n); }
    void setUrl(const QString &u) { if (urlEdit_) urlEdit_->setPlainText(u); }

private:
    QLineEdit *nameEdit_ = nullptr;
    QPlainTextEdit *urlEdit_ = nullptr;
    QLabel *telegramHint_ = nullptr;
    QCheckBox *enabledCheck_ = nullptr;
    QLineEdit *remarkEdit_ = nullptr;
};

/// 订阅源同质化分析与反向剔除对话框
class HomogeneityDialog : public QDialog {
public:
    HomogeneityDialog(const QVector<Models::SourceHomogeneityReport> &reports,
                      std::function<void(const QVector<QString> &, bool)> onExecute,
                      QWidget *parent = nullptr)
        : QDialog(parent), reports_(reports), onExecute_(std::move(onExecute))
    {
        setWindowTitle(QStringLiteral("订阅源同质化分析与反向剔除"));
        resize(780, 520);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(16, 14, 16, 14);
        root->setSpacing(10);

        // 摘要卡片
        auto *summaryCard = new QFrame(this);
        Theme::decorateCard(summaryCard);
        auto *summaryLayout = new QVBoxLayout(summaryCard);
        summaryLayout->setContentsMargins(12, 10, 12, 10);
        summaryLayout->setSpacing(4);

        int redundantCount = 0;
        for (const auto &r : reports_) {
            if (r.isRedundant) {
                ++redundantCount;
            }
        }

        auto *headerLabel = new QLabel(
            QStringLiteral("共检测 %1 个订阅源，发现 %2 个高同质化/冗余源")
                .arg(reports_.size()).arg(redundantCount), summaryCard);
                Gui::Theme::setWidgetCss(headerLabel, redundantCount > 0
            ? QStringLiteral("font-weight: 700; color: {warning}; font-size: 13px;")
            : QStringLiteral("font-weight: 700; color: {success}; font-size: 13px;"));
        summaryLayout->addWidget(headerLabel);

        auto *descLabel = new QLabel(
            QStringLiteral("说明：同质化源指“独有可用节点数为 0 且重合率高于 85%”或全失效源。此类源的所有可用节点均已存在于其它优质源中，剔除后不会丢失任何可用节点，并大幅节省网络与测速资源。勾选后要剔除的源，再点底部按钮执行。"),
            summaryCard);
        descLabel->setWordWrap(true);
        Gui::Theme::setWidgetCss(descLabel, QStringLiteral("color: {muted}; font-size: 11px;"));
        summaryLayout->addWidget(descLabel);

        root->addWidget(summaryCard);

        // 表格
        table_ = new QTableWidget(this);
        table_->setColumnCount(7);
        table_->setHorizontalHeaderLabels({
            QStringLiteral("剔除"),
            QStringLiteral("订阅源名称"),
            QStringLiteral("节点总数"),
            QStringLiteral("可用节点"),
            QStringLiteral("重合率"),
            QStringLiteral("独有可用"),
            QStringLiteral("判定建议")
        });
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table_->setAlternatingRowColors(true);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);

        table_->setRowCount(reports_.size());
        for (int i = 0; i < reports_.size(); ++i) {
            const auto &rep = reports_.at(i);

            auto *check = new QCheckBox(table_);
            check->setChecked(rep.isRedundant);
            connect(check, &QCheckBox::toggled, this, [this]() { updateSelectedCount(); });
            checks_.push_back(check);

            auto *checkContainer = new QWidget(table_);
            auto *cbLayout = new QHBoxLayout(checkContainer);
            cbLayout->setContentsMargins(0, 0, 0, 0);
            cbLayout->setAlignment(Qt::AlignCenter);
            cbLayout->addWidget(check);
            table_->setCellWidget(i, 0, checkContainer);

            auto *nameItem = new QTableWidgetItem(rep.sourceName);
            nameItem->setFont(QFont(font().family(), -1, QFont::Bold));
            table_->setItem(i, 1, nameItem);

            table_->setItem(i, 2, new QTableWidgetItem(QString::number(rep.totalNodes)));

            auto *aliveItem = new QTableWidgetItem(QString::number(rep.aliveNodes));
            if (rep.aliveNodes > 0) {
                aliveItem->setForeground(Gui::Theme::current().success);
            }
            table_->setItem(i, 3, aliveItem);

            auto *overlapItem = new QTableWidgetItem(QStringLiteral("%1%").arg(QString::number(rep.overlapRatePct, 'f', 1)));
            if (rep.overlapRatePct >= 90.0) {
                overlapItem->setForeground(Gui::Theme::current().danger);
            }
            table_->setItem(i, 4, overlapItem);

            auto *uniqueItem = new QTableWidgetItem(QString::number(rep.uniqueAliveNodes));
            uniqueItem->setFont(QFont(font().family(), -1, QFont::Bold));
            uniqueItem->setForeground(rep.uniqueAliveNodes > 0 ? Gui::Theme::current().success : Gui::Theme::current().danger);
            table_->setItem(i, 5, uniqueItem);

            auto *suggItem = new QTableWidgetItem(rep.isRedundant ? QStringLiteral("⚠️ 建议剔除") : QStringLiteral("✓ 核心保留"));
            suggItem->setForeground(rep.isRedundant ? Gui::Theme::current().danger : Gui::Theme::current().success);
            table_->setItem(i, 6, suggItem);
        }

        root->addWidget(table_, 1);

        // 快速选择栏
        auto *selBar = new QHBoxLayout();
        auto *selRedundantBtn = new QPushButton(QStringLiteral("仅勾选冗余源"), this);
        auto *selAllBtn = new QPushButton(QStringLiteral("全选"), this);
        auto *selNoneBtn = new QPushButton(QStringLiteral("清空勾选"), this);
        countStatusLabel_ = new QLabel(this);
        Gui::Theme::setWidgetCss(countStatusLabel_, QStringLiteral("font-weight: 700; color: {accent};"));

        connect(selRedundantBtn, &QPushButton::clicked, this, [this]() {
            for (int i = 0; i < reports_.size(); ++i) {
                checks_[i]->setChecked(reports_[i].isRedundant);
            }
            updateSelectedCount();
        });
        connect(selAllBtn, &QPushButton::clicked, this, [this]() {
            for (auto *c : checks_) c->setChecked(true);
            updateSelectedCount();
        });
        connect(selNoneBtn, &QPushButton::clicked, this, [this]() {
            for (auto *c : checks_) c->setChecked(false);
            updateSelectedCount();
        });

        selBar->addWidget(selRedundantBtn);
        selBar->addWidget(selAllBtn);
        selBar->addWidget(selNoneBtn);
        selBar->addSpacing(12);
        selBar->addWidget(countStatusLabel_);
        selBar->addStretch();
        root->addLayout(selBar);

        // 底部执行栏
        auto *bottomBar = new QHBoxLayout();
        disableBtn_ = new QPushButton(QStringLiteral("批量禁用勾选的源"), this);
        disableBtn_->setObjectName(QStringLiteral("Primary"));

        deleteBtn_ = new QPushButton(QStringLiteral("批量彻底删除"), this);
        deleteBtn_->setObjectName(QStringLiteral("Danger"));

        auto *closeBtn = new QPushButton(QStringLiteral("关闭"), this);

        connect(disableBtn_, &QPushButton::clicked, this, [this]() {
            executePrune(false);
        });
        connect(deleteBtn_, &QPushButton::clicked, this, [this]() {
            executePrune(true);
        });
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

        bottomBar->addWidget(disableBtn_);
        bottomBar->addWidget(deleteBtn_);
        bottomBar->addStretch();
        bottomBar->addWidget(closeBtn);
        root->addLayout(bottomBar);

        updateSelectedCount();
    }

private:
    void updateSelectedCount()
    {
        int selected = 0;
        for (auto *c : checks_) {
            if (c->isChecked()) ++selected;
        }
        countStatusLabel_->setText(QStringLiteral("已勾选 %1 个").arg(selected));
        disableBtn_->setText(QStringLiteral("批量禁用勾选的源 (%1)").arg(selected));
        deleteBtn_->setText(QStringLiteral("批量彻底删除 (%1)").arg(selected));
        disableBtn_->setEnabled(selected > 0);
        deleteBtn_->setEnabled(selected > 0);
    }

    void executePrune(bool deleteRecords)
    {
        QVector<QString> ids;
        for (int i = 0; i < checks_.size(); ++i) {
            if (checks_[i]->isChecked()) {
                ids.push_back(reports_[i].sourceId);
            }
        }
        if (ids.isEmpty()) {
            return;
        }
        if (deleteRecords) {
            const auto answer = QMessageBox::question(
                this, QStringLiteral("确认彻底删除"),
                QStringLiteral("将彻底删除选中的 %1 个订阅源及其节点数据，不可撤销。是否继续？").arg(ids.size()),
                QMessageBox::Yes | QMessageBox::No);
            if (answer != QMessageBox::Yes) {
                return;
            }
        }
        if (onExecute_) {
            onExecute_(ids, deleteRecords);
        }
        accept();
    }

    QVector<Models::SourceHomogeneityReport> reports_;
    std::function<void(const QVector<QString> &, bool)> onExecute_;
    QVector<QCheckBox *> checks_;
    QTableWidget *table_ = nullptr;
    QLabel *countStatusLabel_ = nullptr;
    QPushButton *disableBtn_ = nullptr;
    QPushButton *deleteBtn_ = nullptr;
};

} // namespace

SourcesPage::SourcesPage(App::AppContext &context, QWidget *parent)
    : Page(context, parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 14, 18, 14);
    root->setSpacing(10);

    root->addWidget(buildHeader());

    subPanels_ = new QStackedWidget(this);

    // 面板 0: 订阅列表
    auto *listPanel = new QWidget(this);
    auto *listBox = new QVBoxLayout(listPanel);
    listBox->setContentsMargins(0, 0, 0, 0);
    listBox->setSpacing(10);
    listBox->addWidget(buildActionBar());
    listBox->addWidget(buildGistBar());
    listBox->addWidget(buildSourcesTable(), 1);
    subPanels_->addWidget(listPanel);

    // 面板 1: 拉取结果
    subPanels_->addWidget(buildResultsTable());

    root->addWidget(subPanels_, 1);

    // Ctrl+V 的导入快捷键上移到 MainWindow 了（任何一页都能粘贴导入），这里只留
    // "导入剪贴板"那颗按钮；两边都挂会弹两次确认框。
    connect(&context_, &App::AppContext::settingsChanged, this, &SourcesPage::refresh);
    connect(&context_, &App::AppContext::proxyStatusChanged, this, &SourcesPage::refresh);
}

QWidget *SourcesPage::buildHeader()
{
    auto *header = new QWidget(this);
    auto *vbox = new QVBoxLayout(header);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(4);

    auto *topRow = new QHBoxLayout();
    topRow->addWidget(makeHeaderTitle(QStringLiteral("订阅源"), header));
    auto *sub = new QLabel(QStringLiteral("增删改查 · 单源/全部拉取"), header);
    Gui::Theme::setWidgetCss(sub, QStringLiteral("color: {muted}; font-size: 12px;"));
    topRow->addWidget(sub);
    topRow->addStretch();
    vbox->addLayout(topRow);

    // 订阅列表 / 订阅拉取结果 切换标签
    auto *tabRow = new QHBoxLayout();
    tabRow->setSpacing(6);

    listTabBtn_ = new QPushButton(QStringLiteral("订阅列表"), header);
    listTabBtn_->setObjectName(QStringLiteral("Pill"));
    listTabBtn_->setCheckable(true);
    listTabBtn_->setChecked(true);

    resultsTabBtn_ = new QPushButton(QStringLiteral("订阅拉取结果"), header);
    resultsTabBtn_->setObjectName(QStringLiteral("Pill"));
    resultsTabBtn_->setCheckable(true);
    resultsTabBtn_->setChecked(false);

    connect(listTabBtn_, &QPushButton::clicked, this, [this]() {
        listTabBtn_->setChecked(true);
        resultsTabBtn_->setChecked(false);
        subPanels_->setCurrentIndex(0);
    });
    connect(resultsTabBtn_, &QPushButton::clicked, this, [this]() {
        listTabBtn_->setChecked(false);
        resultsTabBtn_->setChecked(true);
        subPanels_->setCurrentIndex(1);
    });

    tabRow->addWidget(listTabBtn_);
    tabRow->addWidget(resultsTabBtn_);
    tabRow->addStretch();
    vbox->addLayout(tabRow);

    return header;
}

QWidget *SourcesPage::buildActionBar()
{
    auto *bar = new QWidget(this);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    addBtn_ = new QPushButton(QStringLiteral("新增订阅源"), bar);
    addBtn_->setObjectName(QStringLiteral("Primary"));
    connect(addBtn_, &QPushButton::clicked, this, &SourcesPage::openCreateForm);
    layout->addWidget(addBtn_);

    importBtn_ = new QPushButton(QStringLiteral("导入订阅源 (Ctrl+V)"), bar);
    importBtn_->setToolTip(QStringLiteral("识别并导入剪贴板中的订阅链接 (支持单个/多个 URL、节点链接或 JSON，快捷键 Ctrl+V)"));
    connect(importBtn_, &QPushButton::clicked, this, &SourcesPage::importFromClipboard);
    layout->addWidget(importBtn_);

    exportBtn_ = new QPushButton(QStringLiteral("导出订阅源"), bar);
    connect(exportBtn_, &QPushButton::clicked, this, &SourcesPage::exportToClipboard);
    layout->addWidget(exportBtn_);

    fetchAllBtn_ = new QPushButton(QStringLiteral("拉取全部启用源"), bar);
    fetchAllBtn_->setObjectName(QStringLiteral("Primary"));
    fetchAllBtn_->setToolTip(QStringLiteral("后台拉取期间仍可删除/编辑"));
    connect(fetchAllBtn_, &QPushButton::clicked, this, &SourcesPage::fetchAllEnabled);
    layout->addWidget(fetchAllBtn_);

    fetchProxyBtn_ = new QPushButton(QStringLiteral("代理拉取全部"), bar);
    fetchProxyBtn_->setToolTip(QStringLiteral("通过当前运行中的本地代理拉取全部启用源"));
    connect(fetchProxyBtn_, &QPushButton::clicked, this, &SourcesPage::fetchAllViaProxy);
    layout->addWidget(fetchProxyBtn_);

    refreshBtn_ = new QPushButton(QStringLiteral("刷新"), bar);
    connect(refreshBtn_, &QPushButton::clicked, this, &SourcesPage::reloadSources);
    layout->addWidget(refreshBtn_);

    pruneBtn_ = new QPushButton(QStringLiteral("反向剔除同质化源"), bar);
    pruneBtn_->setToolTip(QStringLiteral("分析所有源的重合率，识别出无独有可用节点的同质化冗余源；在弹窗里勾选要处理的源，再批量禁用或彻底删除"));
    connect(pruneBtn_, &QPushButton::clicked, this, &SourcesPage::analyzeHomogeneity);
    layout->addWidget(pruneBtn_);

    autoFetchLabel_ = new QLabel(bar);
    Gui::Theme::setWidgetCss(autoFetchLabel_, QStringLiteral("color: {success}; font-size: 11px;"));
    layout->addWidget(autoFetchLabel_);

    layout->addStretch();
    return bar;
}

QWidget *SourcesPage::buildGistBar()
{
    auto *bar = new QWidget(this);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *label = new QLabel(QStringLiteral("Gist 发现:"), bar);
    Gui::Theme::setWidgetCss(label, QStringLiteral("color: {muted};"));
    layout->addWidget(label);

    const auto &queries = Collector::gistDiscoveryQueries();
    for (const QString &q : queries) {
        QString display = q;
        if (display.endsWith(QStringLiteral("//"))) {
            display.chop(2);
        }
        auto *cb = new QCheckBox(display, bar);
        cb->setChecked(q == QLatin1String(Collector::kGistDiscoveryDefaultQuery));
        cb->setToolTip(QStringLiteral("Gist 发现搜索词“%1”，按勾选的词逐个扫描").arg(q));
        gistQueryChecks_.push_back(cb);
        layout->addWidget(cb);
    }

    gistPagesSpin_ = new QSpinBox(bar);
    gistPagesSpin_->setRange(1, 300);
    gistPagesSpin_->setValue(10);
    gistPagesSpin_->setSuffix(QStringLiteral(" 页"));
    gistPagesSpin_->setToolTip(QStringLiteral("Gist 发现最多扫描的页数（每页 10 条，按最近更新排序）"));
    layout->addWidget(gistPagesSpin_);

    discoverGistsBtn_ = new QPushButton(QStringLiteral("发现 Gist 节点"), bar);
    discoverGistsBtn_->setToolTip(QStringLiteral("扫描 gist.github.com 搜索结果，解析出节点的 gist 自动注册为订阅源"));
    connect(discoverGistsBtn_, &QPushButton::clicked, this, &SourcesPage::discoverGists);
    layout->addWidget(discoverGistsBtn_);

    layout->addStretch();
    return bar;
}

QWidget *SourcesPage::buildSourcesTable()
{
    auto *card = new QFrame(this);
    Theme::decorateCard(card);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);

    sourcesTable_ = new QTableWidget(card);
    sourcesTable_->setObjectName(QStringLiteral("SourcesTable"));
    sourcesTable_->setColumnCount(6);
    sourcesTable_->setHorizontalHeaderLabels({
        QStringLiteral("名称"),
        QStringLiteral("URL 模板"),
        QStringLiteral("状态"),
        QStringLiteral("最近拉取"),
        QStringLiteral("健康"),
        QStringLiteral("操作")
    });
    sourcesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    sourcesTable_->setColumnWidth(0, 150);
    sourcesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    sourcesTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    sourcesTable_->setColumnWidth(2, 140);
    sourcesTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    sourcesTable_->setColumnWidth(3, 140);
    sourcesTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Interactive);
    sourcesTable_->setColumnWidth(4, 100);
    sourcesTable_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Interactive);
    sourcesTable_->setColumnWidth(5, 410);
    sourcesTable_->verticalHeader()->setDefaultSectionSize(76);
    sourcesTable_->setAlternatingRowColors(true);
    sourcesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    sourcesTable_->verticalHeader()->setVisible(false);

    cardLayout->addWidget(sourcesTable_);
    return card;
}

QWidget *SourcesPage::buildResultsTable()
{
    auto *card = new QFrame(this);
    Theme::decorateCard(card);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);

    resultsTable_ = new QTableWidget(card);
    resultsTable_->setColumnCount(5);
    resultsTable_->setHorizontalHeaderLabels({
        QStringLiteral("订阅"),
        QStringLiteral("结果"),
        QStringLiteral("节点数"),
        QStringLiteral("最终 URL"),
        QStringLiteral("详情")
    });
    resultsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    resultsTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    resultsTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    resultsTable_->setAlternatingRowColors(true);
    resultsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultsTable_->verticalHeader()->setVisible(false);

    cardLayout->addWidget(resultsTable_);
    return card;
}

std::optional<QString> SourcesPage::subscriptionProxyUrl() const
{
    const auto &proxyStatus = context_.proxy().status();
    if (proxyStatus.localProxyActive()) {
        return QStringLiteral("http://127.0.0.1:%1").arg(proxyStatus.mixedPort);
    }
    return std::nullopt;
}

void SourcesPage::refresh()
{
    const auto &settings = context_.settings();
    if (settings.autoFetchEnabled) {
        autoFetchLabel_->setText(QStringLiteral("自动拉取 每 %1 分钟").arg(settings.autoFetchIntervalMin));
        autoFetchLabel_->setVisible(true);
    } else {
        autoFetchLabel_->setVisible(false);
    }

    fetchProxyBtn_->setEnabled(!busy_ && subscriptionProxyUrl().has_value());
    reloadSources();
}

void SourcesPage::reloadSources()
{
    QElapsedTimer clock;
    clock.start();
    auto sourcesResult = context_.database().listSources();
    if (sourcesResult.isOk()) {
        sources_ = sourcesResult.takeValue();
    } else {
        sources_.clear();
    }

    auto groupsResult = context_.database().listNodeGroups(std::nullopt, std::nullopt, std::nullopt);
    groupAliveCounts_.clear();
    if (groupsResult.isOk()) {
        for (const auto &g : groupsResult.value()) {
            if (!g.sourceId.isEmpty()) {
                groupAliveCounts_[g.sourceId] = g.alive;
            }
        }
    }
    const qint64 queryMs = clock.elapsed();

    populateSourcesTable();
    // 取证：这两条查询 + 填表全在 GUI 线程同步跑。
    if (queryMs > 30) {
        Log::warn(QStringLiteral("订阅源页同步查库耗时 %1 ms（源 %2 个）")
                      .arg(queryMs).arg(sources_.size()));
    }
}

void SourcesPage::populateSourcesTable()
{
    sourcesTable_->setRowCount(sources_.size());
    const bool proxyAvailable = subscriptionProxyUrl().has_value();

    for (int i = 0; i < sources_.size(); ++i) {
        writeSourceRow(i, sources_.at(i), proxyAvailable);
    }
}

void SourcesPage::writeSourceRow(int row, const Models::Source &s, bool proxyAvailable)
{
    sourcesTable_->setRowHeight(row, 76);
    const QString sourceId = s.id;

    // 0: 名称
    auto *nameItem = new QTableWidgetItem(s.name);
    nameItem->setFont(QFont(font().family(), -1, QFont::Bold));
    nameItem->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    sourcesTable_->setItem(row, 0, nameItem);

    // 1: URL 模板
    auto *urlItem = new QTableWidgetItem(s.urlTemplate);
    urlItem->setToolTip(s.urlTemplate);
    urlItem->setForeground(Gui::Theme::current().accent);
    urlItem->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    sourcesTable_->setItem(row, 1, urlItem);

    // 2: 状态容器
    auto *statusContainer = new QWidget(sourcesTable_);
    auto *statusBox = new QVBoxLayout(statusContainer);
    statusBox->setContentsMargins(6, 6, 6, 6);
    statusBox->setSpacing(2);
    statusBox->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    auto *badge = new QLabel(s.enabled ? QStringLiteral("启用") : QStringLiteral("停用"), statusContainer);
            Gui::Theme::setWidgetCss(badge, s.enabled
        ? QStringLiteral("color: {success}; font-weight: 700;")
        : QStringLiteral("color: {danger}; font-weight: 700;"));
    statusBox->addWidget(badge);

    if (s.pushDisabled) {
        auto *pushLabel = new QLabel(QStringLiteral("不推送"), statusContainer);
        Gui::Theme::setWidgetCss(pushLabel, QStringLiteral("color: {warning}; font-size: 10px;"));
        statusBox->addWidget(pushLabel);
    }

    auto *lastStatusLabel = new QLabel(s.lastStatus.isEmpty() ? QStringLiteral("-") : s.lastStatus, statusContainer);
    Gui::Theme::setWidgetCss(lastStatusLabel, QStringLiteral("color: {muted}; font-size: 11px;"));
    statusBox->addWidget(lastStatusLabel);

    if (s.failStreak >= Collector::kDegradedFailStreak) {
        auto *streakLabel = new QLabel(
            QStringLiteral("连续网络超时 %1 次，仅单次尝试").arg(s.failStreak), statusContainer);
        Gui::Theme::setWidgetCss(streakLabel, QStringLiteral("color: {danger}; font-size: 10px;"));
        statusBox->addWidget(streakLabel);
    }

    // 同质化报告标记
    for (const auto &rep : homogeneityReports_) {
        if (rep.sourceId == s.id) {
            auto *homoBadge = new QLabel(statusContainer);
            if (rep.isRedundant) {
                homoBadge->setText(QStringLiteral("⚠️ 同质化 %1%").arg(QString::number(rep.overlapRatePct, 'f', 0)));
                Gui::Theme::setWidgetCss(homoBadge, QStringLiteral("color: {danger}; font-size: 10px;"));
            } else if (rep.uniqueAliveNodes > 0) {
                homoBadge->setText(QStringLiteral("独有可用 %1").arg(rep.uniqueAliveNodes));
                Gui::Theme::setWidgetCss(homoBadge, QStringLiteral("color: {success}; font-size: 10px;"));
            }
            statusBox->addWidget(homoBadge);
            break;
        }
    }
    sourcesTable_->setCellWidget(row, 2, statusContainer);

    // 3: 最近拉取
    auto *recentContainer = new QWidget(sourcesTable_);
    auto *recentBox = new QVBoxLayout(recentContainer);
    recentBox->setContentsMargins(6, 6, 6, 6);
    recentBox->setSpacing(2);
    recentBox->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    QString timeStr = QStringLiteral("-");
    if (s.lastFetchAt.hasValue) {
        timeStr = TimeUtil::formatLocalReadable(s.lastFetchAt.nanos);
    }
    auto *timeLabel = new QLabel(timeStr, recentContainer);
    timeLabel->setStyleSheet(QStringLiteral("font-size: 11px;"));
    recentBox->addWidget(timeLabel);

    auto *parsedLabel = new QLabel(QStringLiteral("最近解析 %1").arg(s.lastNodeCount), recentContainer);
    Gui::Theme::setWidgetCss(parsedLabel, QStringLiteral("color: {muted}; font-size: 11px;"));
    recentBox->addWidget(parsedLabel);
    sourcesTable_->setCellWidget(row, 3, recentContainer);

    // 4: 健康
    auto *healthContainer = new QWidget(sourcesTable_);
    auto *healthBox = new QVBoxLayout(healthContainer);
    healthBox->setContentsMargins(6, 6, 6, 6);
    healthBox->setSpacing(2);
    healthBox->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    const qint64 total = std::max<qint64>(0, s.lastNodeCount);
    const qint64 alive = groupAliveCounts_.value(s.id, 0);
    auto *healthLabel = new QLabel(healthContainer);
    if (total == 0) {
        healthLabel->setText(QStringLiteral("无节点"));
        Gui::Theme::setWidgetCss(healthLabel, QStringLiteral("color: {warning}; font-size: 11px;"));
    } else {
        healthLabel->setText(QStringLiteral("测通 %1/%2").arg(alive).arg(total));
        Gui::Theme::setWidgetCss(healthLabel, QStringLiteral("color: {textSecondary}; font-size: 11px;"));
    }
    healthBox->addWidget(healthLabel);
    sourcesTable_->setCellWidget(row, 4, healthContainer);

    // 5: 操作按钮
    auto *actionsWidget = new QWidget(sourcesTable_);
    auto *actionsBox = new QHBoxLayout(actionsWidget);
    actionsBox->setContentsMargins(4, 8, 4, 8);
    actionsBox->setSpacing(5);
    actionsBox->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    const bool retry = !s.lastStatus.isEmpty() &&
                       (s.lastStatus.contains(QStringLiteral("失败")) ||
                        s.lastStatus.contains(QStringLiteral("超时")) ||
                        s.lastStatus.contains(QStringLiteral("error")));
    auto *fetchBtn = new QPushButton(retry ? QStringLiteral("重试") : QStringLiteral("拉取"), actionsWidget);
    fetchBtn->setObjectName(QStringLiteral("Small"));
    fetchBtn->setFixedHeight(28);
    fetchBtn->setEnabled(!busy_);
    connect(fetchBtn, &QPushButton::clicked, this, [this, sourceId, fetchBtn]() {
        fetchBtn->setText(QStringLiteral("⏳"));
        fetchBtn->setEnabled(false);
        fetchOne(sourceId);
    });
    actionsBox->addWidget(fetchBtn);

    auto *fetchProxyBtn = new QPushButton(QStringLiteral("代理拉取"), actionsWidget);
    fetchProxyBtn->setObjectName(QStringLiteral("Small"));
    fetchProxyBtn->setFixedHeight(28);
    fetchProxyBtn->setEnabled(!busy_ && proxyAvailable);
    fetchProxyBtn->setToolTip(QStringLiteral("通过当前运行中的本地代理拉取此订阅源"));
    connect(fetchProxyBtn, &QPushButton::clicked, this, [this, sourceId, fetchProxyBtn]() {
        fetchProxyBtn->setText(QStringLiteral("⏳"));
        fetchProxyBtn->setEnabled(false);
        fetchOneViaProxy(sourceId);
    });
    actionsBox->addWidget(fetchProxyBtn);

    auto *editBtn = new QPushButton(QStringLiteral("编辑"), actionsWidget);
    editBtn->setObjectName(QStringLiteral("Small"));
    editBtn->setFixedHeight(28);
    connect(editBtn, &QPushButton::clicked, this, [this, s]() { openEditForm(s); });
    actionsBox->addWidget(editBtn);

    auto *toggleBtn = new QPushButton(s.enabled ? QStringLiteral("禁用") : QStringLiteral("启用"), actionsWidget);
    toggleBtn->setObjectName(QStringLiteral("Small"));
    toggleBtn->setFixedHeight(28);
    connect(toggleBtn, &QPushButton::clicked, this, [this, sourceId, s]() {
        toggleSourceEnabled(sourceId, !s.enabled);
    });
    actionsBox->addWidget(toggleBtn);

    auto *pushBtn = new QPushButton(s.pushDisabled ? QStringLiteral("恢复推送") : QStringLiteral("不推送"), actionsWidget);
    pushBtn->setObjectName(QStringLiteral("Small"));
    pushBtn->setFixedHeight(28);
    connect(pushBtn, &QPushButton::clicked, this, [this, sourceId, s]() {
        toggleSourcePush(sourceId, !s.pushDisabled);
    });
    actionsBox->addWidget(pushBtn);

    auto *deleteBtn = new QPushButton(QStringLiteral("删除"), actionsWidget);
    deleteBtn->setObjectName(QStringLiteral("Danger"));
    deleteBtn->setFixedHeight(28);
    connect(deleteBtn, &QPushButton::clicked, this, [this, sourceId]() { deleteSource(sourceId); });
    actionsBox->addWidget(deleteBtn);

    sourcesTable_->setCellWidget(row, 5, actionsWidget);
}

/// 只重写这一行：其它行的 QTableWidgetItem 与 cellWidget 指针原封不动（见守卫
/// tests/guiSourcesPage refreshSourceRowRebuildsOnlyThatRow）。位置对不上（源集合变了、
/// 表被别处重建过）就退回整表重建，不能在错行上刷。
void SourcesPage::refreshSourceRow(const QString &sourceId)
{
    int row = -1;
    for (int i = 0; i < sources_.size(); ++i) {
        if (sources_.at(i).id == sourceId) {
            row = i;
            break;
        }
    }
    auto sourceResult = context_.database().getSource(sourceId);
    if (row < 0 || sourceResult.isErr() || sourcesTable_->rowCount() != sources_.size()) {
        reloadSources();
        return;
    }

    // 健康列的"测通"口径只有 listNodeGroups 一份，不另写 SQL；这里同样走它，但只把这一源
    // 的计数写回缓存，其它源沿用上次整表刷新时的值。
    const auto groupsResult = context_.database().listNodeGroups(std::nullopt, std::nullopt, std::nullopt);
    if (groupsResult.isOk()) {
        bool found = false;
        qint64 alive = 0;
        for (const auto &g : groupsResult.value()) {
            if (g.sourceId == sourceId) {
                found = true;
                alive = g.alive;
            }
        }
        if (found) {
            groupAliveCounts_[sourceId] = alive;
        } else {
            groupAliveCounts_.remove(sourceId);
        }
    }

    sources_[row] = sourceResult.takeValue();
    writeSourceRow(row, sources_.at(row), subscriptionProxyUrl().has_value());
}

void SourcesPage::populateResultsTable()
{
    resultsTable_->setRowCount(fetchResults_.size());
    for (int i = 0; i < fetchResults_.size(); ++i) {
        const auto &res = fetchResults_.at(i);

        auto *nameItem = new QTableWidgetItem(res.sourceName);
        nameItem->setFont(QFont(font().family(), -1, QFont::Bold));
        resultsTable_->setItem(i, 0, nameItem);

        auto *statusItem = new QTableWidgetItem(res.success ? QStringLiteral("成功") : QStringLiteral("失败"));
        statusItem->setForeground(res.success ? Gui::Theme::current().success : Gui::Theme::current().danger);
        resultsTable_->setItem(i, 1, statusItem);

        resultsTable_->setItem(i, 2, new QTableWidgetItem(QString::number(res.nodeCount)));

        auto *urlItem = new QTableWidgetItem(res.resolvedUrl);
        urlItem->setToolTip(res.resolvedUrl);
        urlItem->setForeground(Gui::Theme::current().accent);
        resultsTable_->setItem(i, 3, urlItem);

        resultsTable_->setItem(i, 4, new QTableWidgetItem(res.message));
    }
}

void SourcesPage::openCreateForm()
{
    SourceEditDialog dlg(nullptr, this);
    if (dlg.exec() == QDialog::Accepted) {
        Models::CreateSource input;
        input.name = dlg.name();
        input.urlTemplate = dlg.url();
        input.enabled = dlg.isEnabled();
        input.remark = dlg.remark();

        auto created = context_.database().createSource(input);
        if (created.isOk()) {
            emit statusMessageRequested(QStringLiteral("订阅源已创建: %1").arg(input.name));
            reloadSources();
        } else {
            QMessageBox::critical(this, QStringLiteral("创建失败"), created.error());
        }
    }
}

void SourcesPage::openEditForm(const Models::Source &source)
{
    SourceEditDialog dlg(&source, this);
    if (dlg.exec() == QDialog::Accepted) {
        Models::UpdateSource patch;
        patch.name = dlg.name();
        patch.urlTemplate = dlg.url();
        patch.enabled = dlg.isEnabled();
        patch.remark = dlg.remark().isEmpty() ? std::nullopt : std::optional<QString>(dlg.remark());

        auto updated = context_.database().updateSource(source.id, patch);
        if (updated.isOk()) {
            emit statusMessageRequested(QStringLiteral("订阅源已更新: %1").arg(dlg.name()));
            reloadSources();
        } else {
            QMessageBox::critical(this, QStringLiteral("更新失败"), updated.error());
        }
    }
}

const QString &SourcesPage::urlTemplateHelp()
{
    static const QString text = QStringLiteral(
        "URL 里的日期占位符会在每次拉取时按当天（本地日期）展开，存进库的是原样模板。\n"
        "\n"
        "年　{Y} {YYYY} → 2026　　{y} {yyyy} → 26\n"
        "月　{mm} {MM} → 07　　　{m} {M} → 7\n"
        "日　{dd} {DD} → 05　　　{d} {D} → 5\n"
        "整串 {Ymd} → 20260705　　{ymd} → 260705\n"
        "\n"
        "例：https://host/{Y}/{mm}/sub?token=x → https://host/2026/07/sub?token=x\n"
        "没列在上面的写法不会展开，会原样留在 URL 里（多半直接 404）。");
    return text;
}

/// 建议名只取 host。带日期占位符的模板（`{Y}/{mm}/2-{Ymd}`）直接喂给 QUrl 是解析不出
/// host 的（实测 `urlHost(原串)` 返回空），所以先按拉取时同一套展开规则过一遍 ——
/// 存进库的仍然是原模板，每次拉取都按当天日期重算。
QString SourcesPage::hostForSuggestedName(const QString &urlTemplate)
{
    const QUrl url(Parser::Common::expandUrlTemplate(urlTemplate));
    QString host = url.host();
    if (host.startsWith(QStringLiteral("www."), Qt::CaseInsensitive)) {
        host.remove(0, 4);
    }
    return host;
}

void SourcesPage::importFromClipboard()
{
    const QString text = QApplication::clipboard()->text().trimmed();
    if (text.isEmpty()) {
        emit statusMessageRequested(QStringLiteral("剪贴板内容为空"));
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("剪贴板内容为空，请先复制订阅链接。"));
        return;
    }

    // 1. 先尝试解析为 JSON 格式订阅源列表（保持原有导出格式兼容）
    QJsonParseError jsonErr;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &jsonErr);
    if (jsonErr.error == QJsonParseError::NoError && doc.isArray()) {
        const QJsonArray array = doc.array();
        if (!array.isEmpty()) {
            int imported = 0;
            int skipped = 0;
            for (const auto &val : array) {
                if (!val.isObject()) continue;
                const QJsonObject obj = val.toObject();
                const QString name = obj.value(QStringLiteral("name")).toString().trimmed();
                const QString url = obj.value(QStringLiteral("url_template")).toString().trimmed();
                if (name.isEmpty() || url.isEmpty()) continue;

                bool exists = false;
                for (const auto &s : sources_) {
                    if (s.name == name && s.urlTemplate == url) {
                        exists = true;
                        break;
                    }
                }
                if (exists) {
                    ++skipped;
                    continue;
                }

                Models::CreateSource input;
                input.name = name;
                input.urlTemplate = url;
                input.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
                if (obj.contains(QStringLiteral("remark"))) {
                    input.remark = obj.value(QStringLiteral("remark")).toString();
                }

                if (context_.database().createSource(input).isOk()) {
                    ++imported;
                }
            }

            emit statusMessageRequested(
                QStringLiteral("订阅源导入完成：新增 %1，跳过重复 %2").arg(imported).arg(skipped));
            reloadSources();
            return;
        }
    }

    // 2. 检查是否为节点分享链接（如 vless://, vmess://, ss://, trojan:// 等）或 base64 节点串
    const bool looksLikeNodeLink = text.contains(QStringLiteral("://"))
        && (text.contains(QStringLiteral("vless://"), Qt::CaseInsensitive)
            || text.contains(QStringLiteral("vmess://"), Qt::CaseInsensitive)
            || text.contains(QStringLiteral("ss://"), Qt::CaseInsensitive)
            || text.contains(QStringLiteral("trojan://"), Qt::CaseInsensitive)
            || text.contains(QStringLiteral("hysteria2://"), Qt::CaseInsensitive)
            || text.contains(QStringLiteral("hy2://"), Qt::CaseInsensitive));
    if (looksLikeNodeLink || (!text.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
                              && !text.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)
                              && !text.startsWith(QStringLiteral("t.me/"), Qt::CaseInsensitive)
                              && text.length() > 64)) {
        const auto parsedNodes = Parser::parseSubscription(text, QString());
        if (!parsedNodes.isEmpty()) {
            const auto choice = QMessageBox::question(
                this, QStringLiteral("识别到代理节点"),
                QStringLiteral("从剪贴板识别到 %1 个节点链接（如「%2」等）。\n\n是否直接导入到节点池？")
                    .arg(parsedNodes.size())
                    .arg(parsedNodes.first().name),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (choice == QMessageBox::Yes) {
                const auto res = context_.database().importNodes(parsedNodes);
                if (res.isOk()) {
                    emit statusMessageRequested(QStringLiteral("已成功导入 %1 个节点到节点池").arg(res.value()));
                    QMessageBox::information(this, QStringLiteral("导入成功"),
                        QStringLiteral("已成功导入 %1 个节点到节点池，可切换至「节点池」查看并测速。").arg(res.value()));
                } else {
                    QMessageBox::warning(this, QStringLiteral("导入失败"),
                        QStringLiteral("导入节点失败: %1").arg(res.error()));
                }
            }
            return;
        }
    }

    // 3. 提取所有 HTTP/HTTPS/Telegram 链接
    QStringList urls;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    for (const QString &rawLine : lines) {
        QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        if (line.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("t.me/"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("@"))) {
            urls.append(line);
        } else {
            static const QRegularExpression urlPattern(QStringLiteral(R"(https?://[^\s<>"]+)"), QRegularExpression::CaseInsensitiveOption);
            auto match = urlPattern.match(line);
            if (match.hasMatch()) {
                urls.append(match.captured(0));
            }
        }
    }

    if (urls.isEmpty()) {
        emit statusMessageRequested(QStringLiteral("剪贴板中未识别到有效订阅链接或节点"));
        QMessageBox::warning(this, QStringLiteral("未能识别"),
            QStringLiteral("剪贴板内容未能识别为有效的订阅源链接（http://、https://、t.me/）或节点链接。"));
        return;
    }

    // 单个 URL：智能解析与导入
    if (urls.size() == 1) {
        const QString urlStr = urls.first();
        QString suggestedName = hostForSuggestedName(urlStr);
        if (suggestedName.isEmpty()) {
            const QString channel = Collector::Telegram::channelName(urlStr);
            if (!channel.isEmpty()) {
                suggestedName = channel;
            } else {
                suggestedName = QStringLiteral("新订阅源");
            }
        }

        // 检查是否已存在（第二条比较容忍 QUrl 往返后的规范化差异，和改名前的行为一致）
        QString existingId;
        QString existingName;
        const QString normalized = QUrl(urlStr).toString();
        for (const auto &s : sources_) {
            if (s.urlTemplate.trimmed() == urlStr || s.urlTemplate.trimmed() == normalized) {
                existingId = s.id;
                existingName = s.name;
                break;
            }
        }

        if (!existingId.isEmpty()) {
            const auto choice = QMessageBox::question(
                this, QStringLiteral("订阅源已存在"),
                QStringLiteral("识别到剪贴板订阅源：\n%1\n\n该订阅源已存在于列表中（名称: %2）。\n是否立即拉取更新？")
                    .arg(urlStr, existingName),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (choice == QMessageBox::Yes) {
                fetchOne(existingId);
            }
            return;
        }

        // 提示快捷导入
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(QStringLiteral("识别到订阅源"));
        msgBox.setText(QStringLiteral("已从剪贴板识别到订阅源链接：\n\n【建议名称】 %1\n【订阅地址】 %2")
            .arg(suggestedName, urlStr));
        msgBox.setInformativeText(QStringLiteral("请选择操作："));
        QPushButton *importFetchBtn = msgBox.addButton(QStringLiteral("立即导入并拉取"), QMessageBox::AcceptRole);
        importFetchBtn->setObjectName(QStringLiteral("Primary"));
        QPushButton *editBtn = msgBox.addButton(QStringLiteral("编辑详细信息"), QMessageBox::ActionRole);
        QPushButton *cancelBtn = msgBox.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
        msgBox.setDefaultButton(importFetchBtn);
        msgBox.exec();

        if (msgBox.clickedButton() == importFetchBtn) {
            Models::CreateSource input;
            input.name = suggestedName;
            input.urlTemplate = urlStr;
            input.enabled = true;
            const auto res = context_.database().createSource(input);
            if (res.isOk()) {
                const QString newId = res.value().id;
                emit statusMessageRequested(QStringLiteral("订阅源「%1」导入成功，正在拉取...").arg(suggestedName));
                reloadSources();
                fetchOne(newId);
            } else {
                QMessageBox::warning(this, QStringLiteral("导入失败"),
                    QStringLiteral("添加订阅源失败: %1").arg(res.error()));
            }
        } else if (msgBox.clickedButton() == editBtn) {
            SourceEditDialog dlg(nullptr, this);
            dlg.setName(suggestedName);
            dlg.setUrl(urlStr);
            if (dlg.exec() == QDialog::Accepted) {
                Models::CreateSource input;
                input.name = dlg.name();
                input.urlTemplate = dlg.url();
                input.enabled = dlg.isEnabled();
                input.remark = dlg.remark();
                const auto res = context_.database().createSource(input);
                if (res.isOk()) {
                    const QString newId = res.value().id;
                    emit statusMessageRequested(QStringLiteral("订阅源「%1」添加成功，正在拉取...").arg(input.name));
                    reloadSources();
                    fetchOne(newId);
                } else {
                    QMessageBox::warning(this, QStringLiteral("添加失败"),
                        QStringLiteral("添加订阅源失败: %1").arg(res.error()));
                }
            }
        }
        return;
    }

    // 多个 URL：批量导入
    const auto choice = QMessageBox::question(
        this, QStringLiteral("批量导入订阅源"),
        QStringLiteral("从剪贴板识别到 %1 个订阅源链接，是否批量导入并立即拉取？").arg(urls.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (choice == QMessageBox::Yes) {
        int imported = 0;
        int skipped = 0;
        for (const QString &url : urls) {
            bool exists = false;
            for (const auto &s : sources_) {
                if (s.urlTemplate == url) {
                    exists = true;
                    break;
                }
            }
            if (exists) {
                ++skipped;
                continue;
            }
            const QString host = hostForSuggestedName(url);
            QString name = host.isEmpty() ? QStringLiteral("订阅-%1").arg(imported + 1) : host;
            Models::CreateSource input;
            input.name = name;
            input.urlTemplate = url;
            input.enabled = true;
            if (context_.database().createSource(input).isOk()) {
                ++imported;
            }
        }
        emit statusMessageRequested(
            QStringLiteral("批量导入完成：新增 %1，跳过重复 %2").arg(imported).arg(skipped));
        reloadSources();
        if (imported > 0) {
            fetchAllEnabled();
        }
    }
}

void SourcesPage::exportToClipboard()
{
    QJsonArray array;
    for (const auto &s : sources_) {
        QJsonObject obj;
        obj[QStringLiteral("name")] = s.name;
        obj[QStringLiteral("url_template")] = s.urlTemplate;
        obj[QStringLiteral("enabled")] = s.enabled;
        if (!s.remark.isEmpty()) {
            obj[QStringLiteral("remark")] = s.remark;
        }
        array.append(obj);
    }
    const QJsonDocument doc(array);
    QApplication::clipboard()->setText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
    emit statusMessageRequested(QStringLiteral("已导出 %1 个订阅源到剪贴板").arg(sources_.size()));
}

void SourcesPage::fetchAllEnabled()
{
    if (busy_) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("后台任务正在运行中，请稍候..."));
        return;
    }
    busy_ = true;
    fetchAllBtn_->setEnabled(false);
    fetchAllBtn_->setText(QStringLiteral("⏳ 正在拉取全部..."));
    emit statusMessageRequested(QStringLiteral("正在拉取全部启用源..."));

    auto db = context_.databasePtr();
    auto tasks = context_.tasks();
    auto resultBox = std::make_shared<Result<QVector<Models::FetchResult>>>(
        Result<QVector<Models::FetchResult>>::withValue({}));
    const auto proxyUrlOpt = subscriptionProxyUrl();

    // 拉取全程以前是零反馈：Collector 有进度回调（对应 Rust 的 fetch_all_enabled_with_progress），
    // 但这里走的是 fetchAllEnabled() 那条 nullptr 版本，界面从点下去到整批结束一动不动。
    // 进度按 80ms 合并，一条状态行文字足够，不必每源砸一次布局重排。
    QPointer<SourcesPage> page(this);
    auto throttle = std::make_shared<Gui::ProgressThrottle>();
    const Collector::Collector::ProgressCallback reportProgress = [page, throttle](qint64 done, qint64 total) {
        if (!throttle->shouldPost(size_t(done), size_t(total))) {
            return;
        }
        QMetaObject::invokeMethod(qApp, [page, done, total]() {
            if (page) {
                page->showFetchProgress(done, total);
            }
        }, Qt::QueuedConnection);
    };

    context_.runAsync([db, tasks, proxyUrlOpt, resultBox, reportProgress]() {
        Collector::Collector collector(db, tasks);
        auto res = collector.fetchAllEnabledWithProgress(reportProgress);
        if (res.isOk() && proxyUrlOpt.has_value()) {
            auto vec = res.takeValue();
            for (auto &item : vec) {
                if (!item.success) {
                    const auto retryRes = collector.fetchOneWithProxy(item.sourceId, proxyUrlOpt.value());
                    if (retryRes.isOk() && retryRes.value().success) {
                        item = retryRes.value();
                    }
                }
            }
            res = Result<QVector<Models::FetchResult>>::withValue(vec);
        }
        *resultBox = res;
    }, [this, resultBox]() {
        busy_ = false;
        fetchAllBtn_->setEnabled(true);
        fetchAllBtn_->setText(QStringLiteral("拉取全部启用源"));
        if (resultBox->isOk()) {
            fetchResults_ = resultBox->takeValue();
            populateResultsTable();
            emit statusMessageRequested(QStringLiteral("拉取完成：共 %1 个订阅源，详情看「订阅拉取结果」")
                                            .arg(fetchResults_.size()));
        } else {
            emit statusMessageRequested(QStringLiteral("拉取失败: %1").arg(resultBox->error()));
        }
        reloadSources();
    }, this);
}

void SourcesPage::showFetchProgress(qint64 done, qint64 total)
{
    emit statusMessageRequested(QStringLiteral("正在拉取订阅源 %1 / %2...").arg(done).arg(total));
}

void SourcesPage::fetchAllViaProxy()
{
    const auto proxyUrlOpt = subscriptionProxyUrl();
    if (!proxyUrlOpt.has_value()) {
        emit statusMessageRequested(QStringLiteral("请先启动代理，再使用代理拉取订阅"));
        QMessageBox::warning(this, QStringLiteral("代理未启动"),
            QStringLiteral("请先在「代理管理」启动代理，再使用代理拉取订阅。"));
        return;
    }
    if (busy_) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("后台任务正在运行中，请稍候..."));
        return;
    }
    busy_ = true;
    fetchProxyBtn_->setEnabled(false);
    fetchProxyBtn_->setText(QStringLiteral("⏳ 正在代理拉取..."));
    const QString proxyUrl = proxyUrlOpt.value();
    emit statusMessageRequested(QStringLiteral("正在通过代理拉取全部启用源..."));

    auto db = context_.databasePtr();
    auto tasks = context_.tasks();
    auto resultBox = std::make_shared<Result<QVector<Models::FetchResult>>>(
        Result<QVector<Models::FetchResult>>::withValue({}));

    QPointer<SourcesPage> page(this);
    auto throttle = std::make_shared<Gui::ProgressThrottle>();
    const Collector::Collector::ProgressCallback reportProgress = [page, throttle](qint64 done, qint64 total) {
        if (!throttle->shouldPost(size_t(done), size_t(total))) {
            return;
        }
        QMetaObject::invokeMethod(qApp, [page, done, total]() {
            if (page) {
                page->showFetchProgress(done, total);
            }
        }, Qt::QueuedConnection);
    };

    context_.runAsync([db, tasks, proxyUrl, resultBox, reportProgress]() {
        Collector::Collector collector(db, tasks);
        *resultBox = collector.fetchAllEnabledWithProxy(proxyUrl, reportProgress);
    }, [this, resultBox]() {
        busy_ = false;
        fetchProxyBtn_->setEnabled(true);
        fetchProxyBtn_->setText(QStringLiteral("代理拉取全部"));
        if (resultBox->isOk()) {
            fetchResults_ = resultBox->takeValue();
            populateResultsTable();
            emit statusMessageRequested(QStringLiteral("代理拉取完成：共 %1 个订阅源，详情看「订阅拉取结果」")
                                            .arg(fetchResults_.size()));
        } else {
            emit statusMessageRequested(QStringLiteral("代理拉取失败: %1").arg(resultBox->error()));
        }
        reloadSources();
    }, this);
}

void SourcesPage::fetchOne(const QString &sourceId)
{
    if (busy_) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("后台任务正在运行中，请稍候..."));
        return;
    }
    busy_ = true;
    emit statusMessageRequested(QStringLiteral("正在拉取订阅源..."));

    auto db = context_.databasePtr();
    auto tasks = context_.tasks();
    auto resultBox = std::make_shared<Result<Models::FetchResult>>(
        Result<Models::FetchResult>::withValue({}));
    const auto proxyUrlOpt = subscriptionProxyUrl();

    context_.runAsync([db, tasks, sourceId, proxyUrlOpt, resultBox]() {
        Collector::Collector collector(db, tasks);
        auto res = collector.fetchOne(sourceId);
        if ((!res.isOk() || (res.isOk() && !res.value().success)) && proxyUrlOpt.has_value()) {
            const auto proxyRes = collector.fetchOneWithProxy(sourceId, proxyUrlOpt.value());
            if (proxyRes.isOk() && proxyRes.value().success) {
                res = proxyRes;
            }
        }
        *resultBox = res;
    }, [this, resultBox, sourceId]() {
        busy_ = false;
        if (resultBox->isOk()) {
            const auto res = resultBox->takeValue();
            fetchResults_.prepend(res);
            populateResultsTable();
            emit statusMessageRequested(QStringLiteral("拉取完成: %1 (%2 个节点)").arg(res.sourceName).arg(res.nodeCount));
            if (res.success) {
                // 原来这里是一个模态框，关掉之后才 emit nodesFetchedForSource ——
                // 那等于让自动测速排在"等你点确定"后面。状态行已经把结果说清楚了。
                emit nodesFetchedForSource(res.sourceId, res.nodeCount);
            } else {
            }
        } else {
            emit statusMessageRequested(QStringLiteral("拉取失败: %1").arg(resultBox->error()));
            QMessageBox::warning(this, QStringLiteral("拉取失败"),
                QStringLiteral("拉取失败: %1").arg(resultBox->error()));
        }
        // 只重写这一条：整表重建会把其它行的控件全换一遍（选中、滚动、hover 都丢）。
        refreshSourceRow(sourceId);
    }, this);
}

void SourcesPage::fetchOneViaProxy(const QString &sourceId)
{
    const auto proxyUrlOpt = subscriptionProxyUrl();
    if (!proxyUrlOpt.has_value()) {
        emit statusMessageRequested(QStringLiteral("请先启动代理，再使用代理拉取订阅"));
        QMessageBox::warning(this, QStringLiteral("代理未启动"),
            QStringLiteral("请先在「代理管理」启动代理，再使用代理拉取订阅。"));
        return;
    }
    if (busy_) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("后台任务正在运行中，请稍候..."));
        return;
    }
    busy_ = true;
    const QString proxyUrl = proxyUrlOpt.value();
    emit statusMessageRequested(QStringLiteral("正在通过代理拉取订阅源..."));

    auto db = context_.databasePtr();
    auto tasks = context_.tasks();
    auto resultBox = std::make_shared<Result<Models::FetchResult>>(
        Result<Models::FetchResult>::withValue({}));

    context_.runAsync([db, tasks, sourceId, proxyUrl, resultBox]() {
        Collector::Collector collector(db, tasks);
        *resultBox = collector.fetchOneWithProxy(sourceId, proxyUrl);
    }, [this, sourceId, resultBox]() {
        busy_ = false;
        if (resultBox->isOk()) {
            const auto res = resultBox->takeValue();
            fetchResults_.prepend(res);
            populateResultsTable();
            emit statusMessageRequested(QStringLiteral("代理拉取完成: %1 (%2 个节点)").arg(res.sourceName).arg(res.nodeCount));
            if (res.success) {
                QMessageBox::information(this, QStringLiteral("代理拉取成功"),
                    QStringLiteral("订阅源「%1」代理拉取完成，解析到 %2 个节点。").arg(res.sourceName).arg(res.nodeCount));
            } else {
                QMessageBox::warning(this, QStringLiteral("代理拉取未获取节点"),
                    QStringLiteral("订阅源「%1」代理拉取未获得节点: %2").arg(res.sourceName).arg(res.message));
            }
        } else {
            emit statusMessageRequested(QStringLiteral("代理拉取失败: %1").arg(resultBox->error()));
            QMessageBox::warning(this, QStringLiteral("代理拉取失败"),
                QStringLiteral("代理拉取请求失败: %1").arg(resultBox->error()));
        }
        refreshSourceRow(sourceId);
    }, this);
}

void SourcesPage::toggleSourceEnabled(const QString &sourceId, bool enabled)
{
    Models::UpdateSource patch;
    patch.enabled = enabled;
    auto res = context_.database().updateSource(sourceId, patch);
    if (res.isOk()) {
        emit statusMessageRequested(enabled ? QStringLiteral("订阅源已启用") : QStringLiteral("订阅源已禁用"));
        reloadSources();
    }
}

void SourcesPage::toggleSourcePush(const QString &sourceId, bool pushDisabled)
{
    Models::UpdateSource patch;
    patch.pushDisabled = pushDisabled;
    auto res = context_.database().updateSource(sourceId, patch);
    if (res.isOk()) {
        emit statusMessageRequested(pushDisabled ? QStringLiteral("该源已标记为不推送") : QStringLiteral("该源已恢复推送"));
        reloadSources();
    }
}

void SourcesPage::deleteSource(const QString &sourceId)
{
    const auto answer = QMessageBox::question(
        this, QStringLiteral("确认删除"), QStringLiteral("确定删除此订阅源及其关联节点？"),
        QMessageBox::Yes | QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    auto res = context_.database().deleteSource(sourceId);
    if (res.isOk()) {
        emit statusMessageRequested(QStringLiteral("订阅源已删除"));
        reloadSources();
    }
}

void SourcesPage::analyzeHomogeneity()
{
    if (homogeneityBusy_) return;
    homogeneityBusy_ = true;
    pruneBtn_->setText(QStringLiteral("分析中..."));
    emit statusMessageRequested(QStringLiteral("正在分析订阅源同质化与节点重合率..."));

    auto db = context_.databasePtr();
    auto resultBox = std::make_shared<Result<QVector<Models::SourceHomogeneityReport>>>(
        Result<QVector<Models::SourceHomogeneityReport>>::withValue({}));

    context_.runAsync([db, resultBox]() {
        *resultBox = db->analyzeSourceHomogeneity();
    }, [this, resultBox]() {
        homogeneityBusy_ = false;
        pruneBtn_->setText(QStringLiteral("反向剔除同质化源"));
        if (resultBox->isOk()) {
            homogeneityReports_ = resultBox->takeValue();
            populateSourcesTable();

            HomogeneityDialog dlg(homogeneityReports_, [this](const QVector<QString> &ids, bool deleteRecords) {
                auto db = context_.databasePtr();
                auto pruneResult = db->pruneHomogenizedSources(ids, deleteRecords);
                if (pruneResult.isOk()) {
                    emit statusMessageRequested(
                        deleteRecords
                            ? QStringLiteral("已彻底删除 %1 个同质化订阅源").arg(ids.size())
                            : QStringLiteral("已禁用 %1 个同质化订阅源").arg(ids.size()));
                } else {
                    emit statusMessageRequested(QStringLiteral("同质化剔除失败: %1").arg(pruneResult.error()));
                }
                reloadSources();
            }, this);
            dlg.exec();
        } else {
            emit statusMessageRequested(QStringLiteral("同质化分析失败: %1").arg(resultBox->error()));
        }
    }, this);
}

void SourcesPage::discoverGists()
{
    QVector<QString> queries;
    const auto &allQueries = Collector::gistDiscoveryQueries();
    for (int i = 0; i < gistQueryChecks_.size() && i < allQueries.size(); ++i) {
        if (gistQueryChecks_[i]->isChecked()) {
            queries.push_back(allQueries[i]);
        }
    }
    if (queries.isEmpty()) {
        emit statusMessageRequested(QStringLiteral("请先勾选至少一个 Gist 查询词"));
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先勾选至少一个 Gist 查询词"));
        return;
    }

    const auto proxyUrlOpt = subscriptionProxyUrl();
    if (!proxyUrlOpt.has_value()) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("代理未启动"),
            QStringLiteral("GitHub Gist 需要海外网络访问。当前代理未启动，直接访问可能超时或失败。\n是否仍要继续直连发现？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    if (busy_) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("后台任务正在运行中，请稍候..."));
        return;
    }
    busy_ = true;
    discoverGistsBtn_->setEnabled(false);
    discoverGistsBtn_->setText(QStringLiteral("⏳ 正在发现 Gist..."));
    const int pages = gistPagesSpin_->value();
    emit statusMessageRequested(QStringLiteral("正在发现 Gist（最多扫描 %1 页）...").arg(pages));

    auto db = context_.databasePtr();
    auto tasks = context_.tasks();
    auto summaryBox = std::make_shared<Collector::GistDiscoverySummary>();
    const QString proxyUrl = proxyUrlOpt.value_or(QString());

    context_.runAsync([db, tasks, queries, pages, proxyUrl, summaryBox]() {
        Collector::Collector baseCollector(db, tasks);
        Collector::Collector collector = baseCollector;
        if (!proxyUrl.isEmpty()) {
            auto proxied = baseCollector.withProxy(proxyUrl);
            if (proxied.isOk()) {
                collector = proxied.takeValue();
            }
        }
        for (const QString &q : queries) {
            auto res = collector.discoverGists(q, pages);
            if (res.isOk()) {
                const auto &s = res.value();
                summaryBox->registered += s.registered;
                summaryBox->nodes += s.nodes;
                summaryBox->skipped += s.skipped;
                summaryBox->rejected += s.rejected;
                summaryBox->pagesScanned += s.pagesScanned;
            }
        }
    }, [this, summaryBox]() {
        busy_ = false;
        discoverGistsBtn_->setEnabled(true);
        discoverGistsBtn_->setText(QStringLiteral("发现 Gist 节点"));
        emit statusMessageRequested(
            QStringLiteral("Gist 发现完成：扫描 %1 页，新注册 %2 个源，解析 %3 个节点")
                .arg(summaryBox->pagesScanned)
                .arg(summaryBox->registered)
                .arg(summaryBox->nodes));
        QMessageBox::information(
            this, QStringLiteral("Gist 发现完成"),
            QStringLiteral("扫描 %1 页，新注册 %2 个源，解析 %3 个节点。\n（跳过重复/无节点: %4 个）\n订阅源列表已更新。")
                .arg(summaryBox->pagesScanned)
                .arg(summaryBox->registered)
                .arg(summaryBox->nodes)
                .arg(summaryBox->skipped + summaryBox->rejected));
        reloadSources();
    }, this);
}

QWidget *createSourcesPage(App::AppContext &context, QWidget *parent)
{
    return new SourcesPage(context, parent);
}

} // namespace Gui
