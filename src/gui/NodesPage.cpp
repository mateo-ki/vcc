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
#include "gui/NodeText.h"
#include "gui/Pages.h"
#include "gui/Theme.h"
#include "gui/UiText.h"
#include "ip/IpRisk.h"
#include "parser/Subscription.h"
#include "proxy/ProxyManager.h"
#include "speedtest/SpeedTester.h"

namespace Gui {

namespace {

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
    card->setObjectName(QStringLiteral("Card"));
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

    regionCombo_ = new QComboBox(card);
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

    latencyCombo_ = new QComboBox(card);
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

    sortCombo_ = new QComboBox(card);
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
    intensityCombo_ = new QComboBox(bar);
    intensityCombo_->addItem(QStringLiteral("快速测试"), int(Speedtest::SpeedtestIntensity::Fast));
    intensityCombo_->addItem(QStringLiteral("标准测试"), int(Speedtest::SpeedtestIntensity::Standard));
    intensityCombo_->addItem(QStringLiteral("深度测试"), int(Speedtest::SpeedtestIntensity::Deep));
    layout->addWidget(intensityCombo_);

    auto *speedPageBtn = new QPushButton(QStringLiteral("测速本页"), bar);
    speedPageBtn->setObjectName(QStringLiteral("AccentBtn"));
    connect(speedPageBtn, &QPushButton::clicked, this, &NodesPage::speedtestPage);
    layout->addWidget(speedPageBtn);

    auto *speedGroupBtn = new QPushButton(QStringLiteral("测速当前订阅"), bar);
    connect(speedGroupBtn, &QPushButton::clicked, this, &NodesPage::speedtestCurrentGroup);
    layout->addWidget(speedGroupBtn);

    auto *speedAllBtn = new QPushButton(QStringLiteral("测速全部"), bar);
    connect(speedAllBtn, &QPushButton::clicked, this, &NodesPage::speedtestAll);
    layout->addWidget(speedAllBtn);

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

QWidget *NodesPage::makeNodeCard(const Models::NodeSummary &node)
{
    const Proxy::ProxyRuntimeStatus status = context_.proxy().status();
    const bool isSelected = status.nodeId == node.id;

    auto *card = new QFrame(gridHost_);
    card->setObjectName(QStringLiteral("Card"));
    card->setMinimumHeight(58);
    card->setCursor(Qt::PointingHandCursor);
    card->setProperty("nodeId", node.id);
    card->setProperty("nodeName", node.name);
    card->setToolTip(QStringLiteral("%1:%2\n点击连接，右键管理").arg(node.server).arg(node.port));

    auto *box = new QVBoxLayout(card);
    box->setContentsMargins(14, 11, 14, 11);
    box->setSpacing(4);

    auto *name = new QLabel(node.name, card);
    name->setObjectName(isSelected ? QStringLiteral("Accent") : QStringLiteral("Title"));
    box->addWidget(name);

    auto *line = new QHBoxLayout();
    line->setSpacing(8);
    auto *protocol = new QLabel(node.protocol.toUpper(), card);
    protocol->setObjectName(QStringLiteral("Muted"));
    line->addWidget(protocol);

    if (!node.country.isNull() && !node.country.isEmpty()) {
        const CountryDisplay display = countryFlagAndName(node.country);
        auto *where = new QLabel(QStringLiteral("%1 %2").arg(display.flag, display.name), card);
        where->setObjectName(QStringLiteral("Subtitle"));
        line->addWidget(where);
    }
    line->addStretch();

    const LatencyDisplay latency = latencyText(node);
    auto *latLabel = new QLabel(latency.text, card);
    if (!latency.role.isEmpty()) {
        latLabel->setObjectName(latency.role);
    }
    line->addWidget(latLabel);
    box->addLayout(line);

    if (isSelected) {
        card->setAutoFillBackground(true);
        QPalette palette = card->palette();
        palette.setColor(QPalette::Window, Theme::current().accentBg);
        card->setPalette(palette);
    }

    const QString nodeId = node.id;
    const QString nodeName = node.name;
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(card, &QWidget::customContextMenuRequested, this, [this, nodeId, nodeName]() {
        QMenu menu;
        QAction *actSpeed = menu.addAction(QStringLiteral("测速节点"));
        QAction *actDetail = menu.addAction(QStringLiteral("查看详情"));
        QAction *actIp = menu.addAction(QStringLiteral("IP 检测"));
        menu.addSeparator();
        QAction *actDel = menu.addAction(QStringLiteral("删除节点"));

        QAction *picked = menu.exec(QCursor::pos());
        if (picked == nullptr) {
            return;
        }
        if (picked == actSpeed) {
            Speedtest::SpeedTestOptions opt;
            opt.timeoutMs = 5000;
            context_.runAsync([this, nodeId, opt]() {
                Speedtest::SpeedTester tester(context_.database().shared_from_this(), context_.tasks());
                tester.testOne(nodeId, opt);
            }, [this]() {
                reloadNodes();
            });
        } else if (picked == actDetail) {
            showNodeDetails(nodeId);
        } else if (picked == actIp) {
            checkIpRisk(nodeId);
        } else if (picked == actDel) {
            deleteNode(nodeId);
        }
    });

    return card;
}

void NodesPage::applyView()
{
    if (grid_ == nullptr) {
        return;
    }
    while (QLayoutItem *item = grid_->takeAt(0)) {
        if (item->widget() != nullptr) {
            delete item->widget();
        }
        delete item;
    }

    const int columns = gridColumns();
    if (currentNodes_.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral("当前筛选条件下没有节点"), gridHost_);
        empty->setObjectName(QStringLiteral("Subtitle"));
        grid_->addWidget(empty, 0, 0, 1, columns);
    } else {
        for (int index = 0; index < currentNodes_.size(); ++index) {
            grid_->addWidget(makeNodeCard(currentNodes_.at(index)), index / columns, index % columns);
        }
    }
    for (int col = 0; col < columns; ++col) {
        grid_->setColumnStretch(col, 1);
    }

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
        while (QLayoutItem *item = guard->groupsLayout_->takeAt(0)) {
            if (item->widget() != nullptr) {
                delete item->widget();
            }
            delete item;
        }

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
        connect(allBtn, &QPushButton::clicked, guard.data(), [guard]() {
            guard->selectedSourceId_.clear();
            guard->page_ = 1;
            guard->reloadGroups();
            guard->reloadNodes();
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
            connect(btn, &QPushButton::clicked, guard.data(), [guard, sid]() {
                guard->selectedSourceId_ = sid;
                guard->page_ = 1;
                guard->reloadGroups();
                guard->reloadNodes();
            });
            guard->groupsLayout_->addWidget(btn);
        }
        guard->groupsLayout_->addStretch();
    });
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
        guard->applyView();
    });
}

void NodesPage::resizeEvent(QResizeEvent *event)
{
    Page::resizeEvent(event);
    if (!currentNodes_.isEmpty()) {
        applyView();
    }
}

void NodesPage::refresh()
{
    reloadGroups();
    reloadNodes();
}

void NodesPage::speedtestPage()
{
    if (currentNodes_.isEmpty()) {
        return;
    }
    QVector<QString> ids;
    for (const auto &n : currentNodes_) {
        ids.push_back(n.id);
    }
    Speedtest::SpeedTestOptions opt;
    opt.timeoutMs = 5000;
    emit statusMessageRequested(QStringLiteral("正在对当前页节点执行测速..."));
    QPointer<NodesPage> guard(this);
    context_.runAsync([this, ids, opt]() {
        Speedtest::SpeedTester tester(context_.database().shared_from_this(), context_.tasks());
        tester.testMany(ids, opt);
    }, [guard]() {
        if (!guard.isNull()) {
            guard->refresh();
            emit guard->statusMessageRequested(QStringLiteral("本页测速完成"));
        }
    });
}

void NodesPage::speedtestCurrentGroup()
{
    if (selectedSourceId_.isEmpty()) {
        speedtestAll();
        return;
    }
    emit statusMessageRequested(QStringLiteral("正在对当前订阅节点执行测速..."));
    const QString sid = selectedSourceId_;
    QPointer<NodesPage> guard(this);
    context_.runAsync([this, sid]() {
        auto idsRes = context_.database().listNodeIdsForGroup(sid, std::nullopt, std::nullopt, std::nullopt);
        if (idsRes.isOk()) {
            Speedtest::SpeedTestOptions opt;
            opt.timeoutMs = 5000;
            Speedtest::SpeedTester tester(context_.database().shared_from_this(), context_.tasks());
            tester.testMany(idsRes.value(), opt);
        }
    }, [guard]() {
        if (!guard.isNull()) {
            guard->refresh();
            emit guard->statusMessageRequested(QStringLiteral("订阅测速完成"));
        }
    });
}

void NodesPage::speedtestAll()
{
    emit statusMessageRequested(QStringLiteral("正在对全部节点执行测速..."));
    QPointer<NodesPage> guard(this);
    context_.runAsync([this]() {
        Speedtest::SpeedTestOptions opt;
        opt.timeoutMs = 5000;
        Speedtest::SpeedTester tester(context_.database().shared_from_this(), context_.tasks());
        tester.testAllSources(opt);
    }, [guard]() {
        if (!guard.isNull()) {
            guard->refresh();
            emit guard->statusMessageRequested(QStringLiteral("全部测速完成"));
        }
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
