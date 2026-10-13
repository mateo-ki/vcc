#include "gui/CommandPalette.h"

#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include "app/AppContext.h"
#include "db/Database.h"
#include "gui/Theme.h"
#include "models/Node.h"
#include "proxy/ProxyManager.h"

namespace Gui {

CommandPalette::CommandPalette(App::AppContext &context, CommandPaletteHost host, QWidget *parent)
    : QDialog(parent, Qt::FramelessWindowHint | Qt::Dialog),
      context_(context),
      host_(std::move(host))
{
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setFixedWidth(580);
    setMaximumHeight(420);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText(QStringLiteral("搜索命令、快速跳转或选择节点 (↑/↓ 选择, Enter 执行, Esc 退出)..."));
    searchEdit_->setFixedHeight(38);
    searchEdit_->installEventFilter(this);
    layout->addWidget(searchEdit_);

    listWidget_ = new QListWidget(this);
    listWidget_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listWidget_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    listWidget_->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(listWidget_, 1);

    applyPaletteTheme();
    buildDefaultItems();
    renderItems(allItems_);

    connect(searchEdit_, &QLineEdit::textChanged, this, &CommandPalette::onSearchTextChanged);
    connect(listWidget_, &QListWidget::itemActivated, this, &CommandPalette::onItemActivated);
    connect(listWidget_, &QListWidget::itemClicked, this, &CommandPalette::onItemActivated);

    searchEdit_->setFocus();
}

void CommandPalette::applyPaletteTheme()
{
    const Theme::Palette palette = Theme::current();
    const QString bg = palette.panel.name();
    const QString border = palette.border.name();
    const QString text = palette.textPrimary.name();
    const QString muted = palette.textSecondary.name();
    const QString accent = palette.accent.name();
    const QString hover = palette.panelHover.name();
    const QString onAccent = palette.onPrimary.name();

    setStyleSheet(QStringLiteral(
        "QDialog {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 10px;"
        "}"
        "QLineEdit {"
        "  background-color: %6;"
        "  color: %3;"
        "  border: 1px solid %2;"
        "  border-radius: 6px;"
        "  padding: 0 12px;"
        "  font-size: 14px;"
        "}"
        "QLineEdit:focus {"
        "  border: 1px solid %5;"
        "}"
        "QListWidget {"
        "  background-color: transparent;"
        "  border: none;"
        "  outline: none;"
        "}"
        "QListWidget::item {"
        "  padding: 8px 10px;"
        "  border-radius: 6px;"
        "  color: %3;"
        "  font-size: 13px;"
        "}"
        "QListWidget::item:hover {"
        "  background-color: %6;"
        "}"
        "QListWidget::item:selected {"
        "  background-color: %5;"
        "  color: %7;"
        "}"
    ).arg(bg, border, text, muted, accent, hover, onAccent));
}

void CommandPalette::buildDefaultItems()
{
    allItems_.clear();

    // 核心导航命令 (Tab 0: Proxy, 1: Nodes, 2: Sources, 3: Settings, 4: Api)
    allItems_.append({PaletteItem::Kind::Navigation, QStringLiteral("🔌 代理控制台"), QStringLiteral("查看当前活动节点、路由模式与内核状态"), QString(), QString(), [this]() {
        if (host_.selectTab) host_.selectTab(0);
    }});
    allItems_.append({PaletteItem::Kind::Navigation, QStringLiteral("📊 节点池管理"), QStringLiteral("管理与浏览全部代理节点"), QString(), QString(), [this]() {
        if (host_.selectTab) host_.selectTab(1);
    }});
    allItems_.append({PaletteItem::Kind::Navigation, QStringLiteral("📥 订阅源列表"), QStringLiteral("查看订阅更新状态与来源"), QString(), QString(), [this]() {
        if (host_.selectTab) host_.selectTab(2);
    }});
    allItems_.append({PaletteItem::Kind::Navigation, QStringLiteral("⚙️ 系统设置"), QStringLiteral("测速配置、内核选择、系统代理设置"), QString(), QString(), [this]() {
        if (host_.selectTab) host_.selectTab(3);
    }});
    allItems_.append({PaletteItem::Kind::Navigation, QStringLiteral("🌐 本地 API 服务"), QStringLiteral("外部客户端集成与 OpenAPI 控制"), QString(), QString(), [this]() {
        if (host_.selectTab) host_.selectTab(4);
    }});

    // 常用操作
    allItems_.append({PaletteItem::Kind::Action, QStringLiteral("⚡ 一键测速全部节点"), QStringLiteral("启动全节点延迟预筛与真实代理握手"), QString(), QString(), [this]() {
        if (host_.selectTab) host_.selectTab(1);
    }});
    allItems_.append({PaletteItem::Kind::Action, QStringLiteral("🔄 更新所有订阅源"), QStringLiteral("拉取最新订阅并同步节点"), QString(), QString(), [this]() {
        if (host_.selectTab) host_.selectTab(2);
    }});
    allItems_.append({PaletteItem::Kind::Action, QStringLiteral("🛑 停止当前代理内核"), QStringLiteral("断开当前节点并终止代理内核进程"), QString(), QString(), [this]() {
        context_.proxy().stop();
    }});

    // 读取部分已有节点加入候选项
    auto nodesResult = context_.database().listNodesForSpeedtest();
    if (nodesResult.isOk()) {
        const auto &nodes = nodesResult.value();
        const int limit = qMin(int(nodes.size()), 100);
        for (int i = 0; i < limit; ++i) {
            const auto &node = nodes.at(i);
            const QString title = QStringLiteral("🔗 %1").arg(node.name.isEmpty() ? node.server : node.name);
            const QString latencyText = node.latencyMs.has_value() ? QStringLiteral("%1ms").arg(*node.latencyMs) : QStringLiteral("未测速");
            const QString sub = QStringLiteral("%1:%2 (%3) %4")
                                    .arg(node.server)
                                    .arg(node.port)
                                    .arg(node.protocol.toUpper())
                                    .arg(latencyText);
            const QString id = node.id;
            allItems_.append({PaletteItem::Kind::Node, title, sub, QString(), id, [this, id]() {
                if (host_.startNode) {
                    host_.startNode(id);
                }
            }});
        }
    }
}

void CommandPalette::renderItems(const QVector<PaletteItem> &items)
{
    filteredItems_ = items;
    listWidget_->clear();
    for (const auto &item : items) {
        auto *widgetItem = new QListWidgetItem(listWidget_);
        widgetItem->setText(QStringLiteral("%1  —  %2").arg(item.title, item.subtitle));
        listWidget_->addItem(widgetItem);
    }
    if (listWidget_->count() > 0) {
        listWidget_->setCurrentRow(0);
    }
}

void CommandPalette::onSearchTextChanged(const QString &text)
{
    const QString query = text.trimmed();
    if (query.isEmpty()) {
        renderItems(allItems_);
        return;
    }

    QVector<PaletteItem> matched;
    for (const auto &item : allItems_) {
        if (item.title.contains(query, Qt::CaseInsensitive) ||
            item.subtitle.contains(query, Qt::CaseInsensitive)) {
            matched.append(item);
        }
    }
    renderItems(matched);
}

void CommandPalette::onItemActivated(QListWidgetItem *item)
{
    const int row = listWidget_->row(item);
    if (row >= 0 && row < filteredItems_.size()) {
        const auto &paletteItem = filteredItems_.at(row);
        if (paletteItem.action) {
            paletteItem.action();
        }
        accept();
    }
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == searchEdit_ && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Down) {
            int current = listWidget_->currentRow();
            if (current + 1 < listWidget_->count()) {
                listWidget_->setCurrentRow(current + 1);
            }
            return true;
        } else if (keyEvent->key() == Qt::Key_Up) {
            int current = listWidget_->currentRow();
            if (current > 0) {
                listWidget_->setCurrentRow(current - 1);
            }
            return true;
        } else if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            auto *current = listWidget_->currentItem();
            if (current) {
                onItemActivated(current);
            }
            return true;
        } else if (keyEvent->key() == Qt::Key_Escape) {
            reject();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

}
