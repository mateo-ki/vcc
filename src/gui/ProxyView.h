#pragma once

#include <QString>
#include <QVector>

#include "models/Node.h"

namespace Gui {

/// 控制台"可用节点"网格的排序方式，对应 Rust `gui/types.rs` 的 ProxySortBy。
enum class ProxySortBy { Latency, Name };

/// 一行预算好的网格行：把每次重算都要做的国家猜测与小写名提前算掉
/// （Rust 那边是每帧重算约 500 个节点才收进来的）。
struct ProxyViewRow {
    Models::NodeSummary node;
    QString country;
    QString lowerName;
};

/// 过滤 + 排序后的网格视图，按 (数据版本, 地区, 搜索词, 排序) 缓存。
struct ProxyView {
    quint64 rev = 0;
    QString region;
    QString search;
    ProxySortBy sort = ProxySortBy::Latency;
    QVector<ProxyViewRow> rows;
};

/// 对应 Rust `views/proxy.rs::build_proxy_view`：地区药丸（OTHER = 不在收录列表里）、
/// 搜索同时命中名字/服务器/国家，排序是 (未测通, 延迟, 名字) 三元组。
ProxyView buildProxyView(const QVector<Models::NodeSummary> &all, const QString &region,
                         const QString &search, ProxySortBy sort, quint64 rev);

/// 网格上的地区药丸：code 与界面文案的对应关系由 UiText 生成，这里只给顺序与判定用的码。
extern const QVector<QString> kRegionCodes;

}
