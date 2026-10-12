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

namespace Proxy {
struct ProxyRuntimeStatus;
}

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

    /// 测某一个订阅源的节点。给"粘贴导入完自动测速"用（MainWindow 调），也是
    /// "测速当前订阅"那颗按钮的实现；内部用 busy_ 挡住并发。
    void speedtestSource(const QString &sourceId);
    /// 单颗节点测速 / 详情弹窗 / IP 风控：控制台的右键菜单走同一份实现，
    /// 不在外壳里再来一遍（那两处文案与 busy 处理都已经在这些地方）。
    void speedtestNode(const QString &nodeId);
    void showNodeDetails(const QString &nodeId);
    void checkIpRisk(const QString &nodeId);

    /// 一次测速真正用的参数：界面上的强度档 + 「系统设置 → 测速」那一组开关。
    /// 对应 Rust `gui/mod.rs:475 speed_opts()` —— Rust 只有这一个旋钮，界面上**每一**
    /// 条测速入口（含底部状态栏那颗「测速全部」）都走它，所以它必须是 public 的唯一来源。
    Speedtest::SpeedTestOptions speedOptions() const;
    /// 界面上那颗「测速强度」下拉框的当前档位。Rust 侧它是 app 的一个字段
    /// （`speedtest_intensity`，全应用只有一份），所有测速入口都从它取参数。
    Speedtest::SpeedtestIntensity speedtestIntensity() const;

signals:
    void nodeSwitchRequested(const QString &nodeId, const QString &nodeName);
    void statusMessageRequested(const QString &message);

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *buildFilterBar();
    QWidget *buildActionBar();
    QWidget *buildSourcesTabs();
    QWidget *buildPaginationBar();
    QWidget *buildNodeGridArea();

    void reloadNodes();
    void reloadGroups();
    /// 点订阅那一排里的某一颗：只换选中项 + 重读节点，**不重建订阅排**
    /// （Rust `views/nodes.rs:343` 的点击分支就只有这三件事）。
    void selectSource(const QString &sourceId);
    /// 按 `selectedSourceId_` 就地改每颗标签的勾选状态。
    void syncGroupSelection();
    /// force=true（数据变了）永远重建网格；force=false（resize）只在列数/数据/
    /// 选中项指纹变化时重建，拖窗口不再每帧重建 50 张卡。
    void applyView(bool force);
    void updatePaginationLabel();
    int gridColumns() const;
    /// status 由 applyView 整网格取一次传进来：每张卡都调 proxy().status() 会
    /// 反复抢 runtimeMutex_（代理启动/切换期间该锁被秒级持有）。
    /// 卡片本体与控制台是同一个 NodeCardFrame（Rust 也是两个视图共用 compact_node_card），
    /// 这里只负责喂数据 + 挂点击/右键菜单。
    QWidget *makeNodeCard(const Models::NodeSummary &node, const Proxy::ProxyRuntimeStatus &status);

    void speedtestCurrentGroup();
    void speedtestAll();
    /// busy 期间三颗测速按钮全灰（Rust 是 `add_enabled(!self.busy, ...)`），
    /// 「测速当前订阅」还要求选中了订阅。之前没选中时它偷偷退化成测速全部。
    void updateActionEnabled();
    void importFromClipboard();
    void exportNodes(const QString &format);
    void cleanupNodes(const QString &action);
    void deleteNode(const QString &nodeId);

    // 筛选控件
    QLineEdit *protocolEdit_ = nullptr;
    QLineEdit *keywordEdit_ = nullptr;
    QComboBox *regionCombo_ = nullptr;
    QComboBox *latencyCombo_ = nullptr;
    QCheckBox *onlyAvailableCheck_ = nullptr;
    QComboBox *sortCombo_ = nullptr;
    QComboBox *intensityCombo_ = nullptr;

    // 测速按钮（busy 时要能灰掉，所以是成员而不是局部变量）。
    QPushButton *speedGroupBtn_ = nullptr;
    QPushButton *speedAllBtn_ = nullptr;

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

    // 上一次真正渲染出来的网格指纹（resize 路径据此跳过重建）。
    int lastGridColumns_ = -1;
    /// 曾经拉伸过的最大列数：列数变小时据此清掉多出来那几列的拉伸。
    int gridColumnsHighWater_ = 0;
    qint64 lastRenderedCount_ = -1;
    QString lastRenderedFirstId_;
    qint64 lastRenderedTotal_ = -1;
    QString lastRenderedSelectedId_;
};

}
