#include "gui/NodesPage.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>

#include <memory>
#include <optional>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QVBoxLayout>

#include "app/AppContext.h"
#include "db/Database.h"
#include "db/DatabaseExport.h"
#include "export/ClashYamlExport.h"
#include "export/UriExport.h"
#include "gui/CountryNames.h"
#include "gui/NodeCard.h"
#include "gui/NodeText.h"
#include "gui/Pages.h"
#include "gui/Theme.h"
#include "gui/Widgets.h"
#include "gui/UiText.h"
#include "ip/IpRisk.h"
#include "parser/Subscription.h"
#include "proxy/ProxyManager.h"
#include "speedtest/SpeedTester.h"

namespace Gui {

namespace {

/// 清空一个布局里的子控件。三步缺一不可：
/// ① hide() —— setParent(nullptr) 会让它瞬间变成顶层窗口，不先藏就会在桌面上闪一下；
/// ② setParent(nullptr) —— deleteLater 要等事件循环回来才真删，留在父级下就是残影；
/// ③ deleteLater 而不是同步 delete —— 重建网格可以是从卡片自己的点击链里发起的
///    （eventFilter → nodeSwitchRequested → MainWindow → refresh() → applyView），
///    同步删掉正在被派发消息的那张卡就是 use-after-free。
void clearLayout(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }
}

QLabel *makeHeaderTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("Title"));
    return label;
}

}

NodesPage::NodesPage(App::AppContext &context, QWidget *parent) : Page(context, parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 14, 18, 14);
    root->setSpacing(10);

    // 页面标题与副标题
    auto *headerBox = new QVBoxLayout();
    headerBox->setSpacing(2);
    auto *title = makeHeaderTitle(QStringLiteral("节点池"), this);
    auto *sub = new QLabel(QStringLiteral("多源汇聚 · 强度测速 · 智能筛选清理"), this);
    sub->setObjectName(QStringLiteral("Subtitle"));
    headerBox->addWidget(title);
    headerBox->addWidget(sub);
    root->addLayout(headerBox);

    root->addWidget(buildFilterBar());
    root->addWidget(buildActionBar());
    root->addWidget(buildSourcesTabs());
    root->addWidget(buildPaginationBar());
    root->addWidget(buildNodeGridArea(), 1);

    refresh();
}

QWidget *NodesPage::buildFilterBar()
{
    auto *card = new QFrame(this);
    Theme::decorateCard(card);
    Theme::decorateCard(card);
    auto *layout = new QHBoxLayout(card);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(8);

    auto *protoLabel = new QLabel(QStringLiteral("协议"), card);
    protoLabel->setObjectName(QStringLiteral("Subtitle"));
    layout->addWidget(protoLabel);

    protocolEdit_ = new QLineEdit(card);
    protocolEdit_->setPlaceholderText(QStringLiteral("vmess..."));
    protocolEdit_->setFixedWidth(80);
    layout->addWidget(protocolEdit_);

    auto *keyLabel = new QLabel(QStringLiteral("关键字"), card);
    keyLabel->setObjectName(QStringLiteral("Subtitle"));
    layout->addWidget(keyLabel);

    keywordEdit_ = new QLineEdit(card);
    keywordEdit_->setPlaceholderText(QStringLiteral("名称/IP..."));
    keywordEdit_->setFixedWidth(110);
    layout->addWidget(keywordEdit_);

    auto *regionLabel = new QLabel(QStringLiteral("地区"), card);
    regionLabel->setObjectName(QStringLiteral("Subtitle"));
    layout->addWidget(regionLabel);

    regionCombo_ = makeComboBox(card);
    regionCombo_->addItem(QStringLiteral("全部地区"), QStringLiteral("all"));
    regionCombo_->addItem(QStringLiteral("香港"), QStringLiteral("HK"));
    regionCombo_->addItem(QStringLiteral("日本"), QStringLiteral("JP"));
    regionCombo_->addItem(QStringLiteral("美国"), QStringLiteral("US"));
    regionCombo_->addItem(QStringLiteral("新加坡"), QStringLiteral("SG"));
    regionCombo_->addItem(QStringLiteral("台湾"), QStringLiteral("TW"));
    regionCombo_->addItem(QStringLiteral("韩国"), QStringLiteral("KR"));
    regionCombo_->addItem(QStringLiteral("其它"), QStringLiteral("OTHER"));
    layout->addWidget(regionCombo_);

    auto *latencyLabel = new QLabel(QStringLiteral("延迟"), card);
    latencyLabel->setObjectName(QStringLiteral("Subtitle"));
    layout->addWidget(latencyLabel);

    latencyCombo_ = makeComboBox(card);
    latencyCombo_->addItem(QStringLiteral("全部"), QStringLiteral("all"));
    latencyCombo_->addItem(QStringLiteral("可用"), QStringLiteral("alive"));
    latencyCombo_->addItem(QStringLiteral("不可用"), QStringLiteral("dead"));
    latencyCombo_->addItem(QStringLiteral("未测速"), QStringLiteral("untested"));
    layout->addWidget(latencyCombo_);

    onlyAvailableCheck_ = new QCheckBox(QStringLiteral("只看可用"), card);
    onlyAvailableCheck_->setToolTip(QStringLiteral("只显示真实代理验证成功的节点"));
    layout->addWidget(onlyAvailableCheck_);

    auto *sortLabel = new QLabel(QStringLiteral("排序"), card);
    sortLabel->setObjectName(QStringLiteral("Subtitle"));
    layout->addWidget(sortLabel);

    sortCombo_ = makeComboBox(card);
    sortCombo_->addItem(QStringLiteral("延迟"), QStringLiteral("latency"));
    sortCombo_->addItem(QStringLiteral("下载速度"), QStringLiteral("download"));
    sortCombo_->addItem(QStringLiteral("更新时间"), QStringLiteral("updated"));
    sortCombo_->addItem(QStringLiteral("名称"), QStringLiteral("name"));
    layout->addWidget(sortCombo_);

    auto *searchBtn = new QPushButton(QStringLiteral("查询"), card);
    searchBtn->setObjectName(QStringLiteral("Pill"));
    connect(searchBtn, &QPushButton::clicked, this, [this]() {
        page_ = 1;
        reloadNodes();
    });
    layout->addWidget(searchBtn);

    layout->addStretch();
    return card;
}

QWidget *NodesPage::buildActionBar()
{
    auto *bar = new QWidget(this);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    layout->addWidget(new QLabel(QStringLiteral("测速强度:"), bar));
    intensityCombo_ = makeComboBox(bar);
    intensityCombo_->addItem(QStringLiteral("快速测试"), int(Speedtest::SpeedtestIntensity::Fast));
    intensityCombo_->addItem(QStringLiteral("标准测试"), int(Speedtest::SpeedtestIntensity::Standard));
    intensityCombo_->addItem(QStringLiteral("深度测试"), int(Speedtest::SpeedtestIntensity::Deep));
    layout->addWidget(intensityCombo_);

    speedGroupBtn_ = new QPushButton(QStringLiteral("测速当前订阅"), bar);
    speedGroupBtn_->setObjectName(QStringLiteral("AccentBtn"));
    speedGroupBtn_->setToolTip(UiText::speedGroupHover());
    connect(speedGroupBtn_, &QPushButton::clicked, this, &NodesPage::speedtestCurrentGroup);
    layout->addWidget(speedGroupBtn_);

    speedAllBtn_ = new QPushButton(QStringLiteral("测速全部"), bar);
    speedAllBtn_->setToolTip(UiText::speedAllHover());
    connect(speedAllBtn_, &QPushButton::clicked, this, &NodesPage::speedtestAll);
    layout->addWidget(speedAllBtn_);

    // 节点清理菜单
    auto *cleanBtn = new QPushButton(QStringLiteral("节点清理 ▼"), bar);
    auto *cleanMenu = new QMenu(cleanBtn);
    cleanMenu->addAction(QStringLiteral("删除超时/失败节点"), this, [this]() { cleanupNodes(QStringLiteral("dead")); });
    cleanMenu->addAction(QStringLiteral("删除无效主机"), this, [this]() { cleanupNodes(QStringLiteral("invalid")); });
    cleanMenu->addAction(QStringLiteral("🧹 物理去重 (同 IP:Port)"), this, [this]() { cleanupNodes(QStringLiteral("physical")); });
    cleanMenu->addAction(QStringLiteral("去除完全相同节点"), this, [this]() { cleanupNodes(QStringLiteral("dedupe")); });
    cleanMenu->addAction(QStringLiteral("✨ 净化节点名称"), this, [this]() { cleanupNodes(QStringLiteral("clean_names")); });
    cleanMenu->addSeparator();
    auto *clearAction = cleanMenu->addAction(QStringLiteral("清空全部节点"), this, [this]() { cleanupNodes(QStringLiteral("clear_all")); });
    cleanBtn->setMenu(cleanMenu);
    // 标签里已经带了 "▼"，Qt 还会再画一个原生 menu-indicator → 两个箭头。
    Gui::Theme::setWidgetCss(cleanBtn, QStringLiteral(
        "QPushButton::menu-indicator { image: none; width: 0; }"));
    layout->addWidget(cleanBtn);

    // 剪贴板导入
    auto *importBtn = new QPushButton(QStringLiteral("剪贴板导入"), bar);
    connect(importBtn, &QPushButton::clicked, this, &NodesPage::importFromClipboard);
    layout->addWidget(importBtn);

    // 导出菜单
    auto *exportBtn = new QPushButton(QStringLiteral("导出节点 ▼"), bar);
    auto *exportMenu = new QMenu(exportBtn);
    exportMenu->addAction(QStringLiteral("Clash 配置"), this, [this]() { exportNodes(QStringLiteral("clash")); });
    exportMenu->addAction(QStringLiteral("URI 链接"), this, [this]() { exportNodes(QStringLiteral("uri")); });
    exportMenu->addAction(QStringLiteral("JSON 格式"), this, [this]() { exportNodes(QStringLiteral("json")); });
    exportBtn->setMenu(exportMenu);
    // 标签里已经带了 "▼"，Qt 还会再画一个原生 menu-indicator → 两个箭头。
    Gui::Theme::setWidgetCss(exportBtn, QStringLiteral(
        "QPushButton::menu-indicator { image: none; width: 0; }"));
    layout->addWidget(exportBtn);

    auto *refreshBtn = new QPushButton(QStringLiteral("刷新"), bar);
    connect(refreshBtn, &QPushButton::clicked, this, [this]() {
        reloadGroups();
        reloadNodes();
    });
    layout->addWidget(refreshBtn);

    layout->addStretch();
    return bar;
}

QWidget *NodesPage::buildSourcesTabs()
{
    auto *container = new QWidget(this);
    auto *root = new QHBoxLayout(container);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    auto *label = new QLabel(QStringLiteral("订阅:"), container);
    label->setObjectName(QStringLiteral("Subtitle"));
    root->addWidget(label);

    auto *scroll = new QScrollArea(container);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidgetResizable(true);
    scroll->setFixedHeight(36);

    groupsHost_ = new QWidget(scroll);
    groupsLayout_ = new QHBoxLayout(groupsHost_);
    groupsLayout_->setContentsMargins(0, 0, 0, 0);
    groupsLayout_->setSpacing(6);
    scroll->setWidget(groupsHost_);

    root->addWidget(scroll, 1);
    return container;
}

QWidget *NodesPage::buildPaginationBar()
{
    auto *bar = new QWidget(this);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    pageLabel_ = new QLabel(QStringLiteral("第 1 / 1 页 · 共 0 条"), bar);
    pageLabel_->setObjectName(QStringLiteral("Subtitle"));
    layout->addWidget(pageLabel_);

    prevPageBtn_ = new QPushButton(QStringLiteral("上一页"), bar);
    prevPageBtn_->setEnabled(false);
    connect(prevPageBtn_, &QPushButton::clicked, this, [this]() {
        if (page_ > 1) {
            page_ -= 1;
            reloadNodes();
        }
    });
    layout->addWidget(prevPageBtn_);

    nextPageBtn_ = new QPushButton(QStringLiteral("下一页"), bar);
    nextPageBtn_->setEnabled(false);
    connect(nextPageBtn_, &QPushButton::clicked, this, [this]() {
        page_ += 1;
        reloadNodes();
    });
    layout->addWidget(nextPageBtn_);

    layout->addStretch();
    return bar;
}

QWidget *NodesPage::buildNodeGridArea()
{
    nodeScroll_ = new QScrollArea(this);
    nodeScroll_->setObjectName(QStringLiteral("NodeScroll"));
    nodeScroll_->setWidgetResizable(true);
    nodeScroll_->setFrameShape(QFrame::NoFrame);
    nodeScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    gridHost_ = new QWidget(nodeScroll_);
    grid_ = new QGridLayout(gridHost_);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setSpacing(8);
    nodeScroll_->setWidget(gridHost_);
    return nodeScroll_;
}

int NodesPage::gridColumns() const
{
    const int available = nodeScroll_ != nullptr ? nodeScroll_->viewport()->width() : width();
    if (available >= 920) {
        return 4;
    }
    if (available >= 660) {
        return 3;
    }
    return 2;
}

QWidget *NodesPage::makeNodeCard(const Models::NodeSummary &node,
                                 const Proxy::ProxyRuntimeStatus &status)
{
    // 共用控制台那张自绘卡：两行（名字 / 协议 · 国家 · 延迟），配色与三态由
    // NodeCardFrame 的 skin 负责（= Rust 的 panel/accent_bg + stroke）。
    // 之前这里是"每卡一份 QSS"的 QFrame#Card，245 张卡实测多花 ~20MB。
    ProxyViewRow row;
    row.node = node;
    row.country = node.country;
    row.lowerName = node.name.toLower();

    auto *card = new NodeCardFrame(gridHost_);
    card->bind(row, status, status.nodeId);
    card->setCursor(Qt::PointingHandCursor);
    card->setToolTip(QStringLiteral("%1:%2").arg(node.server).arg(node.port)
                     + QChar(10) + QStringLiteral("点击连接，右键管理"));
    card->installEventFilter(this);

    const QString nodeId = node.id;
    const QString nodeName = node.name;
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(card, &QWidget::customContextMenuRequested, this, [this, nodeId, nodeName]() {
        // 菜单条目与控制台共用同一个构造器（文字全来自 Rust 生成的 UiText）。
        // 顺序按 Rust views/nodes.rs：测速 / 详情 / IP 检测 / ── / 删除；
        // 测速那条在 busy 时置灰（Rust 的 add_enabled(!self.busy, ...)）。
        QMenu menu;
        const NodeCardMenu actions = addNodeCardMenu(&menu, true, !busy_, true, false);
        QAction *picked = menu.exec(QCursor::pos());
        if (picked == nullptr) {
            return;
        }
        if (picked == actions.speedtest) {
            speedtestNode(nodeId);
        } else if (picked == actions.details) {
            showNodeDetails(nodeId);
        } else if (picked == actions.ipRisk) {
            checkIpRisk(nodeId);
        } else if (picked == actions.remove) {
            deleteNode(nodeId);
        }
    });

    return card;
}

bool NodesPage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        auto *card = qobject_cast<QWidget *>(watched);
        if (card != nullptr && mouse->button() == Qt::LeftButton) {
            const QString nodeId = card->property("nodeId").toString();
            const QString nodeName = card->property("nodeName").toString();
            if (!nodeId.isEmpty()) {
                emit nodeSwitchRequested(nodeId, nodeName);
                return true;
            }
        }
    }
    return Page::eventFilter(watched, event);
}

void NodesPage::applyView(bool force)
{
    if (grid_ == nullptr) {
        return;
    }
    updateActionEnabled();
    const int columns = gridColumns();
    // 整网格只取一次代理状态：makeNodeCard 每张卡都抢 runtimeMutex_ 的话，
    // 代理启动/切换期间（锁被秒级持有）会把 GUI 线程直接冻住。
    const Proxy::ProxyRuntimeStatus status = context_.proxy().status();

    // resize 路径（force=false）：列数、数据与选中节点都没变就不重建整网格。
    // 拖窗口时每帧重建 50 张卡（各带右键菜单 lambda）是"界面超级卡"的来源之一。
    // 数据路径（force=true）永远重建：测速改延迟后首节点 id 不变，不能靠指纹判断。
    const bool sameLayout = !force
        && columns == lastGridColumns_
        && currentNodes_.size() == lastRenderedCount_
        && lastRenderedFirstId_ == (currentNodes_.isEmpty() ? QString() : currentNodes_.first().id)
        && totalCount_ == lastRenderedTotal_
        && status.nodeId == lastRenderedSelectedId_;
    if (sameLayout) {
        updatePaginationLabel();
        return;
    }

    // 重建期间关掉整棵子树的更新：50 张卡的增删只触发一次布局/绘制。
    gridHost_->setUpdatesEnabled(false);
    clearLayout(grid_);

    if (currentNodes_.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral("当前筛选条件下没有节点"), gridHost_);
        empty->setObjectName(QStringLiteral("Subtitle"));
        grid_->addWidget(empty, 0, 0, 1, columns);
    } else {
        for (int index = 0; index < currentNodes_.size(); ++index) {
            grid_->addWidget(makeNodeCard(currentNodes_.at(index), status),
                             index / columns, index % columns);
        }
    }
    // 列数变小时要清掉多出来那几列的拉伸，否则它们还占着宽度（和 ProxyPage 同一套）。
    for (int col = 0; col < gridColumnsHighWater_; ++col) {
        grid_->setColumnStretch(col, col < columns ? 1 : 0);
        grid_->setColumnMinimumWidth(col, 0);
    }
    gridColumnsHighWater_ = qMax(gridColumnsHighWater_, columns);
    gridHost_->setUpdatesEnabled(true);

    lastGridColumns_ = columns;
    lastRenderedCount_ = currentNodes_.size();
    lastRenderedFirstId_ = currentNodes_.isEmpty() ? QString() : currentNodes_.first().id;
    lastRenderedTotal_ = totalCount_;
    lastRenderedSelectedId_ = status.nodeId;

    updatePaginationLabel();
}

void NodesPage::updatePaginationLabel()
{
    const quint32 totalPages = totalCount_ > 0
        ? static_cast<quint32>((totalCount_ + pageSize_ - 1) / pageSize_)
        : 1;
    pageLabel_->setText(QStringLiteral("第 %1 / %2 页 · 共 %3 条")
                            .arg(page_)
                            .arg(totalPages)
                            .arg(totalCount_));
    prevPageBtn_->setEnabled(page_ > 1);
    nextPageBtn_->setEnabled(page_ < totalPages);
}

void NodesPage::reloadGroups()
{
    auto resultBox = std::make_shared<Result<QVector<Models::NodeGroupSummary>>>(
        Result<QVector<Models::NodeGroupSummary>>::withValue(QVector<Models::NodeGroupSummary>()));
    QPointer<NodesPage> guard(this);
    context_.runAsync([this, resultBox]() {
        *resultBox = context_.database().listNodeGroups(std::nullopt, std::nullopt, std::nullopt);
    }, [guard, resultBox]() {
        if (guard.isNull() || resultBox->isErr()) {
            return;
        }
        guard->groups_ = resultBox->value();
        clearLayout(guard->groupsLayout_);

        // 全部源
        qint64 totalAll = 0;
        qint64 aliveAll = 0;
        for (const auto &g : guard->groups_) {
            totalAll += g.total;
            aliveAll += g.alive;
        }
        auto *allBtn = new QPushButton(QStringLiteral("全部  可用 %1 / 共 %2").arg(aliveAll).arg(totalAll), guard->groupsHost_);
        allBtn->setObjectName(QStringLiteral("Pill"));
        allBtn->setCheckable(true);
        allBtn->setChecked(guard->selectedSourceId_.isEmpty());
        // 选中判据住在属性里：换订阅时只改勾选状态，不重建这一排（Rust 的
        // `views/nodes.rs:343-347` 点击分支里只有 selected + page + reload_nodes）。
        allBtn->setProperty("sourceId", QString());
        connect(allBtn, &QPushButton::clicked, guard.data(), [guard]() {
            guard->selectSource(QString());
        });
        guard->groupsLayout_->addWidget(allBtn);

        for (const auto &group : guard->groups_) {
            const QString sid = group.sourceId;
            const QString label = QStringLiteral("%1  可用 %2 / 共 %3")
                .arg(group.sourceName)
                .arg(group.alive)
                .arg(group.total);
            auto *btn = new QPushButton(label, guard->groupsHost_);
            btn->setObjectName(QStringLiteral("Pill"));
            btn->setCheckable(true);
            btn->setChecked(guard->selectedSourceId_ == sid);
            btn->setProperty("sourceId", sid);
            connect(btn, &QPushButton::clicked, guard.data(), [guard, sid]() {
                guard->selectSource(sid);
            });
            guard->groupsLayout_->addWidget(btn);
        }
        guard->groupsLayout_->addStretch();
    });
}

void NodesPage::selectSource(const QString &sourceId)
{
    // Rust 是 `selectable_label(...).clicked() && !selected`：点已经选中的那一颗
    // 什么都不做，连节点表都不重读。
    if (selectedSourceId_ == sourceId) {
        return;
    }
    selectedSourceId_ = sourceId;
    page_ = 1;
    updateActionEnabled();
    syncGroupSelection();
    reloadNodes();
}

void NodesPage::syncGroupSelection()
{
    // 就地改勾选：重建这一排会先 clearLayout（整排瞬间消失）、再等一次异步
    // listNodeGroups 回来才重新出现，而他只是换了个订阅。
    // 只认带 `sourceId` 属性的那一颗 —— 筛选栏那颗「查询」也挂着 "Pill" 这个
    // objectName，按 objectName 摸会把它一起改到。
    for (int index = 0; index < groupsLayout_->count(); ++index) {
        auto *pill = qobject_cast<QPushButton *>(groupsLayout_->itemAt(index)->widget());
        if (pill != nullptr && pill->property("sourceId").isValid()) {
            pill->setChecked(pill->property("sourceId").toString() == selectedSourceId_);
        }
    }
}

void NodesPage::reloadNodes()
{
    if (loading_) {
        return;
    }
    loading_ = true;

    std::optional<QString> proto;
    if (protocolEdit_ != nullptr && !protocolEdit_->text().trimmed().isEmpty()) {
        proto = protocolEdit_->text().trimmed();
    }
    std::optional<QString> keyword;
    if (keywordEdit_ != nullptr && !keywordEdit_->text().trimmed().isEmpty()) {
        keyword = keywordEdit_->text().trimmed();
    }
    std::optional<QString> region;
    if (regionCombo_ != nullptr && regionCombo_->currentData().toString() != QStringLiteral("all")) {
        region = regionCombo_->currentData().toString();
    }
    std::optional<QString> latency;
    if (onlyAvailableCheck_ != nullptr && onlyAvailableCheck_->isChecked()) {
        latency = QStringLiteral("alive");
    } else if (latencyCombo_ != nullptr && latencyCombo_->currentData().toString() != QStringLiteral("all")) {
        latency = latencyCombo_->currentData().toString();
    }
    std::optional<QString> sort;
    if (sortCombo_ != nullptr) {
        sort = sortCombo_->currentData().toString();
    }

    const QString sid = selectedSourceId_;
    const quint32 curPage = page_;
    const quint32 curPageSize = pageSize_;

    auto resultBox = std::make_shared<Result<Models::Page<Models::NodeSummary>>>(
        Result<Models::Page<Models::NodeSummary>>::withValue(Models::Page<Models::NodeSummary>()));
    QPointer<NodesPage> guard(this);

    context_.runAsync([this, proto, sid, keyword, latency, region, curPage, curPageSize, sort, resultBox]() {
        *resultBox = context_.database().listNodes(proto, sid, keyword, latency, region, curPage, curPageSize, sort);
    }, [guard, resultBox]() {
        if (guard.isNull()) {
            return;
        }
        guard->loading_ = false;
        if (resultBox->isOk()) {
            const auto &p = resultBox->value();
            guard->currentNodes_ = p.items;
            guard->totalCount_ = p.total;
        } else {
            guard->currentNodes_.clear();
            guard->totalCount_ = 0;
            emit guard->statusMessageRequested(resultBox->error());
        }
        guard->applyView(/*force=*/true);
    });
}

void NodesPage::resizeEvent(QResizeEvent *event)
{
    Page::resizeEvent(event);
    if (!currentNodes_.isEmpty()) {
        applyView(/*force=*/false);
    }
}

void NodesPage::refresh()
{
    reloadGroups();
    reloadNodes();
}

Speedtest::SpeedtestIntensity NodesPage::speedtestIntensity() const
{
    return intensityCombo_ != nullptr ? Speedtest::SpeedtestIntensity(intensityCombo_->currentData().toInt())
                                      : Speedtest::defaultIntensity();
}

Speedtest::SpeedTestOptions NodesPage::speedOptions() const
{
    // 强度档位 + 系统设置那一组开关，一起折成这一轮真正用的参数（Rust speed_opts()）。
    return Speedtest::speedOptionsFor(context_.settings(), speedtestIntensity());
}

void NodesPage::updateActionEnabled()
{
    // 对应 Rust 的 add_enabled(!busy, ...) 与 add_enabled(!busy && 选中了订阅, ...)。
    // 灰掉而不是"点了没反应"：之前 busy_ 挡住重入时是静默 return，看着就像界面死了。
    if (speedGroupBtn_ != nullptr) {
        speedGroupBtn_->setEnabled(!busy_ && !selectedSourceId_.isEmpty());
    }
    if (speedAllBtn_ != nullptr) {
        speedAllBtn_->setEnabled(!busy_);
    }
}

void NodesPage::speedtestNode(const QString &nodeId)
{
    // 对应 Rust speedtest_one（gui/mod.rs:1992）：先报"正在测速节点..."，
    // 回来再按 SpeedDone 那套汇总文案收尾，别默默测完。
    if (busy_) {
        return;
    }
    const Speedtest::SpeedTestOptions opt = speedOptions();
    busy_ = true;
    updateActionEnabled();
    emit statusMessageRequested(UiText::speedOneStatus());
    QPointer<NodesPage> guard(this);
    auto outcome = std::make_shared<Result<Models::SpeedTestSummary>>();
    context_.runAsync([this, nodeId, opt, outcome]() {
        Speedtest::SpeedTester tester(context_.database().shared_from_this(), context_.tasks());
        auto one = tester.testOne(nodeId, opt);
        if (one.isErr()) {
            *outcome = Result<Models::SpeedTestSummary>::withError(one.error());
            return;
        }
        const Models::LatencyResult probe = one.value();
        Models::SpeedTestSummary summary;
        summary.total = 1;
        summary.ok = probe.ok ? 1 : 0;
        summary.fail = probe.ok ? 0 : 1;
        summary.avgMs = probe.latencyMs.has_value() ? std::optional<double>(*probe.latencyMs) : std::nullopt;
        summary.avgDownloadKbps = probe.downloadKbps.has_value()
            ? std::optional<double>(*probe.downloadKbps)
            : std::nullopt;
        summary.results = {probe};
        *outcome = Result<Models::SpeedTestSummary>::withValue(std::move(summary));
    }, [guard, outcome]() {
        if (guard.isNull()) {
            return;
        }
        guard->busy_ = false;
        guard->updateActionEnabled();
        guard->reloadNodes();
        emit guard->statusMessageRequested(outcome->isOk() ? speedDoneText(outcome->value())
                                        : UiText::speedDoneFailed().arg(outcome->error()));
    });
}

void NodesPage::speedtestCurrentGroup()
{
    if (selectedSourceId_.isEmpty()) {
        // Rust 这边没有"没选中就测全部"的退路：那颗按钮本来就是灰的。
        return;
    }
    speedtestSource(selectedSourceId_);
}

namespace {

std::optional<QString> trimmedOption(const QLineEdit *edit)
{
    if (edit == nullptr) {
        return std::nullopt;
    }
    const QString text = edit->text().trimmed();
    return text.isEmpty() ? std::nullopt : std::optional<QString>(text);
}

} // namespace

/// 测某一个订阅源的节点（"测速当前订阅"和"粘贴导入后自动测速"共用这一条路）。
/// 筛选条件必须带上：Rust 的 hover 明写"测速当前订阅中**符合筛选条件**的全部节点"。
void NodesPage::speedtestSource(const QString &sourceId)
{
    if (busy_ || sourceId.isEmpty()) {
        return;
    }
    busy_ = true;
    updateActionEnabled();

    QString groupName = sourceId;
    qint64 groupTotal = 0;
    for (const Models::NodeGroupSummary &group : groups_) {
        if (group.sourceId == sourceId) {
            groupName = group.sourceName;
            groupTotal = group.total;
            break;
        }
    }
    emit statusMessageRequested(UiText::speedGroupStatus().arg(groupName).arg(groupTotal));

    std::optional<QString> protocol = trimmedOption(protocolEdit_);
    std::optional<QString> keyword = trimmedOption(keywordEdit_);
    std::optional<QString> latency;
    if (onlyAvailableCheck_ != nullptr && onlyAvailableCheck_->isChecked()) {
        latency = QStringLiteral("alive");
    } else if (latencyCombo_ != nullptr
               && latencyCombo_->currentData().toString() != QStringLiteral("all")) {
        latency = latencyCombo_->currentData().toString();
    }

    const Speedtest::SpeedTestOptions opt = speedOptions();
    const QString sid = sourceId;
    QPointer<NodesPage> guard(this);
    auto outcome = std::make_shared<Result<Models::SpeedTestSummary>>();
    context_.runAsync([this, sid, protocol, keyword, latency, opt, outcome, guard]() {
        auto idsRes = context_.database().listNodeIdsForGroup(sid, protocol, keyword, latency);
        if (!idsRes.isOk()) {
            *outcome = Result<Models::SpeedTestSummary>::withError(idsRes.error());
            return;
        }
        if (idsRes.value().isEmpty()) {
            *outcome = Result<Models::SpeedTestSummary>::withError(UiText::speedGroupEmpty());
            return;
        }
        Speedtest::SpeedTester tester(context_.database().shared_from_this(), context_.tasks());
        *outcome = tester.testManyWithProgress(
            idsRes.value(), opt, [guard](const Speedtest::SpeedTestProgress &p) {
                if (guard.isNull()) {
                    return;
                }
                QMetaObject::invokeMethod(guard.data(), [guard, p]() {
                    if (guard.isNull()) {
                        return;
                    }
                    emit guard->statusMessageRequested(formatSpeedtestProgress(p));
                }, Qt::QueuedConnection);
            });
    }, [guard, outcome]() {
        if (guard.isNull()) {
            return;
        }
        guard->busy_ = false;
        guard->updateActionEnabled();
        guard->refresh();
        emit guard->statusMessageRequested(outcome->isOk() ? speedDoneText(outcome->value())
                                        : UiText::speedDoneFailed().arg(outcome->error()));
    });
}

void NodesPage::speedtestAll()
{
    if (!context_.tryBeginFullSpeedtest()) {
        return;
    }
    busy_ = true;
    updateActionEnabled();
    emit statusMessageRequested(UiText::speedAllInitial());
    QPointer<NodesPage> guard(this);
    auto outcome = std::make_shared<Result<Models::SpeedTestSummary>>();
    const Speedtest::SpeedTestOptions opt = speedOptions();
    context_.runAsync([this, opt, outcome, guard]() {
        Speedtest::SpeedTester tester(context_.database().shared_from_this(), context_.tasks());
        *outcome = tester.testAllSourcesWithProgress(opt, [guard](size_t done, size_t total) {
            if (guard.isNull()) {
                return;
            }
            QMetaObject::invokeMethod(guard.data(), [guard, done, total]() {
                if (guard.isNull()) {
                    return;
                }
                emit guard->statusMessageRequested(
                    UiText::speedAllProgress().arg(qint64(done)).arg(qint64(total)));
            }, Qt::QueuedConnection);
        });
    }, [guard, outcome, ctx = QPointer<App::AppContext>(&context_)]() {
        // 门要在这一趟结束时开回来，哪怕页面已经关了（否则这道门永远关着，之后谁都测不了）。
        if (ctx != nullptr) {
            ctx->endFullSpeedtest();
        }
        if (guard.isNull()) {
            return;
        }
        guard->busy_ = false;
        guard->updateActionEnabled();
        guard->refresh();
        emit guard->statusMessageRequested(outcome->isOk() ? speedDoneText(outcome->value())
                                        : UiText::speedDoneFailed().arg(outcome->error()));
    });
}

void NodesPage::importFromClipboard()
{
    const QString text = QGuiApplication::clipboard()->text().trimmed();
    if (text.isEmpty()) {
        emit statusMessageRequested(QStringLiteral("剪贴板为空"));
        return;
    }
    emit statusMessageRequested(QStringLiteral("正在从剪贴板解析导入..."));
    QPointer<NodesPage> guard(this);
    context_.runAsync([this, text]() {
        const auto nodes = Parser::parseSubscription(text, QString());
        if (!nodes.isEmpty()) {
            context_.database().importNodes(nodes);
        }
    }, [guard]() {
        if (!guard.isNull()) {
            guard->refresh();
            emit guard->statusMessageRequested(QStringLiteral("剪贴板导入完成"));
        }
    });
}

void NodesPage::exportNodes(const QString &format)
{
    auto nodesRes = DbExport::exportNodesFiltered(context_.database(), std::nullopt, selectedSourceId_.isEmpty() ? std::nullopt : std::optional<QString>(selectedSourceId_), true, 800, 50.0, 5000);
    if (nodesRes.isErr() || nodesRes.value().isEmpty()) {
        emit statusMessageRequested(QStringLiteral("没有可导出的节点"));
        return;
    }
    const auto &nodes = nodesRes.value();
    QString exportedText;
    if (format == QStringLiteral("clash")) {
        exportedText = Export::clashProxiesYaml(nodes);
    } else if (format == QStringLiteral("uri")) {
        exportedText = Export::nodesToShareUris(nodes).join(QLatin1Char('\n'));
    } else {
        Json arr = Json::array();
        for (const auto &n : nodes) {
            arr.push_back(n.toJson());
        }
        exportedText = QString::fromStdString(arr.dump(2));
    }
    QGuiApplication::clipboard()->setText(exportedText);
    emit statusMessageRequested(QStringLiteral("已导出 %1 个节点并复制到剪贴板").arg(nodes.size()));
}

void NodesPage::cleanupNodes(const QString &action)
{
    emit statusMessageRequested(QStringLiteral("正在执行节点清理..."));
    QPointer<NodesPage> guard(this);
    context_.runAsync([this, action]() {
        if (action == QStringLiteral("dead")) {
            context_.database().deleteDeadNodes();
        } else if (action == QStringLiteral("invalid")) {
            context_.database().deleteInvalidHosts();
        } else if (action == QStringLiteral("physical")) {
            context_.database().deduplicatePhysical();
        } else if (action == QStringLiteral("dedupe")) {
            context_.database().dedupeNodes();
        } else if (action == QStringLiteral("clean_names")) {
            context_.database().cleanAndBeautifyNodeNames();
        } else if (action == QStringLiteral("clear_all")) {
            context_.database().clearNodes();
        }
    }, [guard]() {
        if (!guard.isNull()) {
            guard->refresh();
            emit guard->statusMessageRequested(QStringLiteral("清理操作完成"));
        }
    });
}

void NodesPage::showNodeDetails(const QString &nodeId)
{
    auto nodeRes = context_.database().getNode(nodeId);
    if (nodeRes.isErr()) {
        QMessageBox::warning(this, QStringLiteral("错误"), nodeRes.error());
        return;
    }
    const auto &node = nodeRes.value();
    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(QStringLiteral("节点详情 - %1").arg(node.name));
    dialog->resize(600, 450);

    auto *box = new QVBoxLayout(dialog);
    auto *edit = new QPlainTextEdit(dialog);
    edit->setReadOnly(true);
    edit->setFont(QFont(QStringLiteral("Consolas"), 10));
    edit->setPlainText(QString::fromStdString(node.toJson().dump(2)));
    box->addWidget(edit);

    auto *btnRow = new QHBoxLayout();
    auto *copyBtn = new QPushButton(QStringLiteral("复制 JSON"), dialog);
    connect(copyBtn, &QPushButton::clicked, dialog, [edit]() {
        QGuiApplication::clipboard()->setText(edit->toPlainText());
    });
    btnRow->addWidget(copyBtn);
    btnRow->addStretch();
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), dialog);
    connect(closeBtn, &QPushButton::clicked, dialog, &QDialog::accept);
    btnRow->addWidget(closeBtn);
    box->addLayout(btnRow);

    dialog->exec();
}

void NodesPage::checkIpRisk(const QString &nodeId)
{
    auto nodeRes = context_.database().getNode(nodeId);
    if (nodeRes.isErr()) {
        return;
    }
    const QString server = nodeRes.value().server;
    emit statusMessageRequested(QStringLiteral("正在检测 IP 风控画像: %1...").arg(server));
    QPointer<NodesPage> guard(this);
    auto riskBox = std::make_shared<Result<IpRisk::Risk>>(Result<IpRisk::Risk>::withValue(IpRisk::Risk()));
    context_.runAsync([server, riskBox]() {
        *riskBox = IpRisk::lookup(server);
    }, [guard, server, riskBox]() {
        if (guard.isNull()) {
            return;
        }
        if (riskBox->isOk()) {
            const auto &risk = riskBox->value();
            QMessageBox::information(guard.data(), QStringLiteral("IP 风控检测结果"),
                                     QStringLiteral("服务器: %1\n落地 IP: %2\n国家: %3\n组织: %4\n机房 IP: %5\n公开代理: %6\n评级: %7")
                                         .arg(server, risk.ip, risk.country, risk.org,
                                              risk.datacenter ? QStringLiteral("是") : QStringLiteral("否"),
                                              risk.publicProxy ? QStringLiteral("是") : QStringLiteral("否"),
                                              risk.verdict));
            emit guard->statusMessageRequested(QStringLiteral("IP 检测完成: %1").arg(risk.headline()));
        } else {
            QMessageBox::warning(guard.data(), QStringLiteral("IP 检测失败"), riskBox->error());
            emit guard->statusMessageRequested(riskBox->error());
        }
    });
}

void NodesPage::deleteNode(const QString &nodeId)
{
    context_.database().deleteNode(nodeId);
    reloadNodes();
    emit statusMessageRequested(QStringLiteral("已删除节点"));
}

QWidget *createNodesPage(App::AppContext &context, QWidget *parent)
{
    return new NodesPage(context, parent);
}

}
