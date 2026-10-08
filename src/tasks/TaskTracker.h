#pragma once

#include <QString>
#include <QVector>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include "core/JsonUtil.h"

namespace Tasks {

constexpr size_t kMaxTaskHistory = 20;

enum class Kind { Fetch, Speedtest };

enum class Phase {
    Queued,
    Downloading,
    Parsing,
    Writing,
    TcpPrefilter,
    GoogleVerify,
    Updating,
    Completed,
    Failed,
};

/// serde 的 rename_all = "snake_case" 取值，界面和 API 都用这两个函数拿名字。
QString kindName(Kind kind);
QString phaseName(Phase phase);
bool isTerminalPhase(Phase phase);

/// 对应 Rust 的 SourceTaskSnapshot。
struct SourceSnapshot {
    QString sourceId;
    QString sourceName;
    Phase phase = Phase::Queued;
    size_t nodeCount = 0;
    std::optional<quint64> downloadMs;
    std::optional<quint64> parseMs;
    std::optional<quint64> writeMs;
    std::optional<quint64> speedtestMs;
    std::optional<quint64> updateMs;
    quint64 elapsedMs = 0;
    std::optional<QString> message;
    std::optional<QString> error;

    Json toJson() const;
};

/// 对应 Rust 的 TaskSnapshot。startedAt/finishedAt 是 UTC epoch 纳秒。
struct Snapshot {
    QString id;
    Kind kind = Kind::Fetch;
    qint64 startedAtNanos = 0;
    std::optional<qint64> finishedAtNanos;
    size_t totalSources = 0;
    size_t completedSources = 0;
    size_t successfulSources = 0;
    size_t failedSources = 0;
    quint64 elapsedMs = 0;
    QVector<SourceSnapshot> sources;

    Json toJson() const;
};

/// 采集/测速的进度台账：界面轮询它，API 把它序列化成 /tasks。
/// 可拷贝，拷贝共享同一份状态（对应 Rust 的 `Clone` + `Arc<Mutex<..>>`）。
class TaskTracker {
public:
    TaskTracker();

    /// 开一轮任务，返回任务 id；sources 是 (source_id, source_name) 列表。
    QString start(Kind kind, const QVector<std::pair<QString, QString>> &sources);

    void phase(const QString &taskId, const QString &sourceId, Phase phase);
    void timings(const QString &taskId, const QString &sourceId,
                 const std::optional<quint64> &downloadMs, const std::optional<quint64> &parseMs,
                 const std::optional<quint64> &writeMs,
                 const std::optional<quint64> &speedtestMs,
                 const std::optional<quint64> &updateMs);
    void nodes(const QString &taskId, const QString &sourceId, size_t nodeCount);
    void complete(const QString &taskId, const QString &sourceId, const QString &message);
    void fail(const QString &taskId, const QString &sourceId, const QString &error);

    QVector<Snapshot> snapshots(size_t limit) const;
    bool isSourceActive(const QString &sourceId) const;
    bool hasActiveFetch() const;

private:
    struct SourceState;
    struct TaskState;
    struct Inner;

    void update(const QString &taskId, const QString &sourceId,
                const std::function<void(SourceSnapshot &)> &mutate) const;
    void finishSource(const QString &taskId, const QString &sourceId, bool ok,
                      const QString &text) const;
    /// 对应 Rust 的 `fn snapshot(&TaskState) -> TaskSnapshot`：未结束的计时现算。
    static Snapshot renderSnapshot(const TaskState &task);

    std::shared_ptr<Inner> inner_;
};

}
