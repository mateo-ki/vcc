#pragma once

#include "gui/Page.h"
#include "models/Node.h"
#include "models/Types.h"
#include "speedtest/SpeedtestTypes.h"

#include <QList>
#include <QString>
#include <QVector>
#include <optional>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
QT_END_NAMESPACE

namespace Gui {

/// 节点池管理页，对应 Rust `gui/views/nodes.rs` 的 `ui_nodes`。
class NodesPage : public Page {
    Q_OBJECT

public:
    explicit NodesPage(App::AppContext &context, QWidget *parent = nullptr);

    void refresh() override;

    int loadedNodeCount() const { return currentNodes_.size(); }
    qint64 totalNodeCount() const { return totalCount_; }
    quint32 currentPage() const { return page_; }

signals:
    void nodeSwitchRequested(const QString &nodeId, const QString &nodeName);
    void statusMessageRequested(const QString &message);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    QWidget *buildFilterBar();
    QWidget *buildActionBar();
    QWidget *buildSourcesTabs();
    QWidget *buildPaginationBar();
    QWidget *buildNodeGridArea();

    void reloadNodes();
    void reloadGroups();
    void applyView();
    int gridColumns() const;
    QWidget *makeNodeCard(const Models::NodeSummary &node);

    void speedtestPage();
    void speedtestCurrentGroup();
    void speedtestAll();
    void importFromClipboard();
    void exportNodes(const QString &format);
    void cleanupNodes(const QString &action);
    void showNodeDetails(const QString &nodeId);
    void checkIpRisk(const QString &nodeId);
    void deleteNode(const QString &nodeId);

    // 筛选控件
    QLineEdit *protocolEdit_ = nullptr;
    QLineEdit *keywordEdit_ = nullptr;
    QComboBox *regionCombo_ = nullptr;
    QComboBox *latencyCombo_ = nullptr;
    QCheckBox *onlyAvailableCheck_ = nullptr;
    QComboBox *sortCombo_ = nullptr;
    QComboBox *intensityCombo_ = nullptr;

    // 订阅标签与分页
    QWidget *groupsHost_ = nullptr;
    QHBoxLayout *groupsLayout_ = nullptr;
    QLabel *pageLabel_ = nullptr;
    QPushButton *prevPageBtn_ = nullptr;
    QPushButton *nextPageBtn_ = nullptr;

    // 网格与卡片
    QScrollArea *nodeScroll_ = nullptr;
    QWidget *gridHost_ = nullptr;
    QGridLayout *grid_ = nullptr;

    // 状态
    QString selectedSourceId_;
    quint32 page_ = 1;
    quint32 pageSize_ = 50;
    qint64 totalCount_ = 0;
    QVector<Models::NodeSummary> currentNodes_;
    QVector<Models::NodeGroupSummary> groups_;
    bool busy_ = false;
    bool loading_ = false;
};

}
