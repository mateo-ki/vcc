#pragma once

#include <QHash>
#include <QString>
#include <QVector>

#include <optional>

#include "api/ApiDepends.h"
#include "api/HttpMessage.h"
#include "models/Node.h"

namespace Api {

/// `{id}` 之类的路径参数。axum 的 `Path<String>` 永远解得开，所以这里不需要错误态。
using PathParams = QHash<QString, QString>;

/// 对应 src/api/mod.rs 里的那些 handler。每个方法都返回完整 Response（状态码 + 信封），
/// 与 Rust 的 `impl IntoResponse` 一一对应；调用方（路由表 / 单测）不需要 socket。
///
/// 【阻塞】方法内部直接查库、起停代理、发网络请求，只能在 worker 线程调用。
class Handlers {
public:
    explicit Handlers(AppState state);

    const AppState &state() const { return state_; }

    // ---- 路由表里的 33 个 handler（fn router 的注册顺序）----
    Response health(const Request &request, const PathParams &params) const;
    Response listTasks(const Request &request, const PathParams &params) const;
    Response kernels(const Request &request, const PathParams &params) const;
    Response kernelReleases(const Request &request, const PathParams &params) const;
    Response updateKernel(const Request &request, const PathParams &params) const;
    Response proxyStatus(const Request &request, const PathParams &params) const;
    Response proxyConfig(const Request &request, const PathParams &params) const;
    Response updateProxyConfig(const Request &request, const PathParams &params) const;
    Response proxyLog(const Request &request, const PathParams &params) const;
    Response proxyStart(const Request &request, const PathParams &params) const;
    Response proxyStop(const Request &request, const PathParams &params) const;
    Response proxyRestart(const Request &request, const PathParams &params) const;
    Response stats(const Request &request, const PathParams &params) const;
    Response getSettings(const Request &request, const PathParams &params) const;
    Response updateSettings(const Request &request, const PathParams &params) const;
    Response listSources(const Request &request, const PathParams &params) const;
    Response getSource(const Request &request, const PathParams &params) const;
    Response createSource(const Request &request, const PathParams &params) const;
    Response updateSource(const Request &request, const PathParams &params) const;
    Response deleteSource(const Request &request, const PathParams &params) const;
    Response fetchSource(const Request &request, const PathParams &params) const;
    Response fetchAll(const Request &request, const PathParams &params) const;
    Response discoverGists(const Request &request, const PathParams &params) const;
    Response listNodes(const Request &request, const PathParams &params) const;
    Response getNode(const Request &request, const PathParams &params) const;
    Response deleteNode(const Request &request, const PathParams &params) const;
    Response clearNodes(const Request &request, const PathParams &params) const;
    Response deleteDeadNodes(const Request &request, const PathParams &params) const;
    Response deleteInvalidHosts(const Request &request, const PathParams &params) const;
    Response dedupeNodes(const Request &request, const PathParams &params) const;
    Response speedtestOne(const Request &request, const PathParams &params) const;
    Response speedtestAll(const Request &request, const PathParams &params) const;
    Response exportJson(const Request &request, const PathParams &params) const;
    Response exportClash(const Request &request, const PathParams &params) const;
    Response exportUri(const Request &request, const PathParams &params) const;

    // ---- Rust 侧私有的辅助函数，公开出来好单测 ----

    /// 对应 `fn error_resp`：500 + 错误信封。
    static Response errorResponse(const QString &message);

    /// 对应 `quality_params(&ExportQuery)`：(quality_only, max_lat, min_dl, limit)。
    struct QualityParams {
        bool qualityOnly = true;
        qint64 maxLatencyMs = 500;
        double minDownloadKbps = 0.0;
        std::optional<quint32> limit;
    };
    struct ExportQuery {
        std::optional<QString> protocol;
        std::optional<QString> sourceId;
        std::optional<bool> qualityOnly;
        std::optional<qint64> maxLatencyMs;
        std::optional<double> minDownloadKbps;
        std::optional<bool> base64;
        std::optional<quint32> limit;
        /// `?protocol=&page=` 这类反序列化失败：非空时路由直接回 400 拒绝响应。
        QString queryError;
    };
    static QualityParams qualityParams(const ExportQuery &query);
    /// 对应 `fn load_export_nodes`（含 quality_only 的排序 + 截断）。
    Result<QVector<Models::Node>> loadExportNodes(const ExportQuery &query) const;
    /// 解析 export_* 的查询串；失败时 queryError 非空（serde_urlencoded 的行为）。
    static ExportQuery parseExportQuery(const Request &request);

    /// 对应 `proxy_operation_error`：节点连不通时顺手把它标成不可用。
    Response proxyOperationError(const QString &nodeId, const QString &message) const;

private:
    AppState state_;
};

} // namespace Api
