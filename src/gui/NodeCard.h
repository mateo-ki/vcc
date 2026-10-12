#pragma once

#include "gui/ProxyView.h"
#include "proxy/MihomoConfig.h"

#include <QColor>
#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QSize>
#include <QString>

QT_BEGIN_NAMESPACE
class QMenu;
class QAction;
QT_END_NAMESPACE

namespace Gui {

class NodeBadge;
class ElidedLabel;

/// 两张页面（控制台「可用节点」与节点池）共用的那一张卡 —— Rust 也是同一个
/// `widgets.rs::compact_node_card` 被 proxy.rs 与 views/nodes.rs 同时调用。
/// 三态。Rust 那边是即时模式，每帧只画可见行、什么都不保留；
/// Qt 这边要留住 245 张卡，所以样式绝对不能按控件走 —— 实测每卡各挂一份
/// setStyleSheet 时，245 张卡多花 20MB，筛选重建还要重解析 1470 份 CSS。
enum class NodeCardState {
    Normal,
    Selected,
    Running,
};

/// 卡片外观：圆角 + 描边 + 三态底色，全部 QPainter 画，不走 QSS。
class NodeCardFrame : public QFrame {
    // Q_OBJECT 是为了让外壳的 eventFilter 能 qobject_cast 出卡片类型、测试也能
    // findChild 到它。每实例多一个指针（245 张卡约 2KB），与"每卡一份 QSS"不是一回事。
    Q_OBJECT

public:
    explicit NodeCardFrame(QWidget *parent = nullptr);

    void setState(NodeCardState state);
    NodeCardState state() const { return state_; }

    /// 重绑一行内容。切地区/搜索时卡片是复用的：只改文字与配色，不重建控件树 ——
    /// 重建一次 245 张卡要新建 1470 个子控件，那才是"筛选地区特别卡"的大头。
    void bind(const ProxyViewRow &row, const Proxy::ProxyRuntimeStatus &status,
              const QString &selectedNodeId);

    QString nodeId() const { return nodeId_; }

    /// 两行文字 + 上下内边距（与 paintEvent 共用同一套字号/边距常量）。
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    // hover 换色：WA_Hover 收到进入/离开事件后要重画（样式表里的 :hover 就是这个语义）。
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Skin {
        QColor fill;
        QColor fillHover;
        QColor border;
        QColor borderHover;
        int borderWidth = 1;
    };

    Skin skinForState() const;

    NodeCardState state_ = NodeCardState::Normal;
    QString nodeId_;
    QString nodeName_;
    // 卡片不养任何子控件：四段文字在 paintEvent 里画（子 QLabel 会被应用级
    // `QWidget { color: textPrimary }` 在 polish 时统一刷成一个颜色，见 cpp 注释）。
    QString nameText_;
    QString protocolText_;
    QString countryText_;
    QString latencyText_;
    QString latencySlot_;
};

/// 卡片上的小徽章（协议名、运行中/已选中）：圆角底色 + 内边距，同样不走 QSS。
class NodeBadge : public QLabel {
public:
    enum class Tone {
        Protocol,
        Running,
        Selected,
    };

    explicit NodeBadge(const QString &text, QWidget *parent = nullptr);

    void setTone(Tone tone);

    // QLabel::sizeHint() 不含 contentsMargins，而 QSS 的 padding 会算进去 ——
    // 不自己加回来的话徽章会比旧样式表版本窄一截。
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    static constexpr int kVerticalPadding = 2;

    Tone tone_ = Tone::Protocol;
    QColor background_;
    int sidePadding_ = 6;
};

/// 节点卡右键菜单的五条动作（不需要的就是 nullptr）。
/// 两条页面共用同一个构造器：条目文字一律取自 Rust 生成的 UiText，顺序按各自视图
/// —— Rust 里 nodes.rs 是「测速/详情/IP/──/删除」，proxy.rs 是「详情/测速/IP/[锁定]」，
/// 所以顺序是参数，文字与可用性判断不是。
struct NodeCardMenu {
    QAction *speedtest = nullptr;
    QAction *details = nullptr;
    QAction *ipRisk = nullptr;
    QAction *remove = nullptr;
    QAction *lockEgress = nullptr;
};

/// 往 menu 里挂卡片菜单。speedtestEnabled 对应 Rust 的 add_enabled(!self.busy, ...)；
/// 删除那条按 Rust 上 danger 前景色（颜色从 Theme 取，不写死、也不挂按控件 QSS）。
NodeCardMenu addNodeCardMenu(QMenu *menu, bool speedtestFirst, bool speedtestEnabled,
                             bool withDelete, bool withLockEgress);

/// 卡片文字用的字体（家族跟着控件自身，字号/字重由调用方给）。
QFont cardFont(const QWidget *widget, int pixelSize, bool bold);

/// 卡片上四段文字各自的矩形（paintEvent 与 sizeHint 共用同一份算法，别各写一遍）。
struct NodeCardTexts {
    QString name;
    QString protocol;
    QString country;
    QString latency;
    QString latencySlot;
};

}
