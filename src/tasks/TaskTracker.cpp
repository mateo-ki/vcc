#include "tasks/TaskTracker.h"

#include <QElapsedTimer>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <vector>

#include "core/Ids.h"
#include "core/TimeUtil.h"

namespace Tasks {
namespace {

Json optionalNumber(const std::optional<quint64> &value)
{
    return value.has_value() ? Json(std::uint64_t(*value)) : Json(nullptr);
}

Json optionalText(const std::optional<QString> &value)
{
    return value.has_value() ? Json(value->toStdString()) : Json(nullptr);
}

Json optionalMillis(const std::optional<qint64> &nanos)
{
    return nanos.has_value()
               ? Json(TimeUtil::formatRfc3339(*nanos, true).toStdString())
               : Json(nullptr);
}

}

QString kindName(Kind kind)
{
    return kind == Kind::Fetch ? QStringLiteral("fetch") : QStringLiteral("speedtest");
}

QString phaseName(Phase phase)
{
    switch (phase) {
    case Phase::Queued:
        return QStringLiteral("queued");
    case Phase::Downloading:
        return QStringLiteral("downloading");
    case Phase::Parsing:
        return QStringLiteral("parsing");
    case Phase::Writing:
        return QStringLiteral("writing");
    case Phase::TcpPrefilter:
        return QStringLiteral("tcp_prefilter");
    case Phase::GoogleVerify:
        return QStringLiteral("google_verify");
    case Phase::Updating:
        return QStringLiteral("updating");
    case Phase::Completed:
        return QStringLiteral("completed");
    case Phase::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("queued");
}

bool isTerminalPhase(Phase phase)
{
    return phase == Phase::Completed || phase == Phase::Failed;
}

Json SourceSnapshot::toJson() const
{
    Json json = Json::object();
    json["source_id"] = sourceId.toStdString();
    json["source_name"] = sourceName.toStdString();
    json["phase"] = phaseName(phase).toStdString();
    json["node_count"] = std::uint64_t(nodeCount);
    json["download_ms"] = optionalNumber(downloadMs);
    json["parse_ms"] = optionalNumber(parseMs);
    json["write_ms"] = optionalNumber(writeMs);
    json["speedtest_ms"] = optionalNumber(speedtestMs);
    json["update_ms"] = optionalNumber(updateMs);
    json["elapsed_ms"] = std::uint64_t(elapsedMs);
    json["message"] = optionalText(message);
    json["error"] = optionalText(error);
    return json;
}

Json Snapshot::toJson() const
{
    Json json = Json::object();
    json["id"] = id.toStdString();
    json["kind"] = kindName(kind).toStdString();
    json["started_at"] = TimeUtil::formatRfc3339(startedAtNanos, true).toStdString();
    json["finished_at"] = optionalMillis(finishedAtNanos);
    json["total_sources"] = std::uint64_t(totalSources);
    json["completed_sources"] = std::uint64_t(completedSources);
    json["successful_sources"] = std::uint64_t(successfulSources);
    json["failed_sources"] = std::uint64_t(failedSources);
    json["elapsed_ms"] = std::uint64_t(elapsedMs);
    Json sources = Json::array();
    for (const SourceSnapshot &source : this->sources) {
        sources.push_back(source.toJson());
    }
    json["sources"] = sources;
    return json;
}

struct TaskTracker::SourceState {
    SourceSnapshot snapshot;
    QElapsedTimer started;
};

struct TaskTracker::TaskState {
    Snapshot snapshot;
    QElapsedTimer started;
    QMap<QString, SourceState> sources;
};

struct TaskTracker::Inner {
    mutable QMutex mutex;
    std::vector<std::unique_ptr<TaskState>> tasks;
};

TaskTracker::TaskTracker() : inner_(std::make_shared<Inner>()) {}

QString TaskTracker::start(Kind kind, const QVector<std::pair<QString, QString>> &sources)
{
    const QString id = Ids::newUuidV4();
    QElapsedTimer started;
    started.start();
    auto task = std::make_unique<TaskState>();
    task->snapshot.id = id;
    task->snapshot.kind = kind;
    task->snapshot.startedAtNanos = TimeUtil::nowUtcNanos();
    task->started = started;
    for (const auto &entry : sources) {
        SourceState state;
        state.snapshot.sourceId = entry.first;
        state.snapshot.sourceName = entry.second;
        state.snapshot.phase = Phase::Queued;
        state.started = started;
        task->sources.insert(entry.first, state);
    }
    task->snapshot.totalSources = size_t(task->sources.size());
    if (task->sources.isEmpty()) {
        task->snapshot.finishedAtNanos = TimeUtil::nowUtcNanos();
    }
    QMutexLocker locker(&inner_->mutex);
    inner_->tasks.insert(inner_->tasks.begin(), std::move(task));
    if (inner_->tasks.size() > kMaxTaskHistory) {
        inner_->tasks.resize(kMaxTaskHistory);
    }
    return id;
}

void TaskTracker::update(const QString &taskId, const QString &sourceId,
                         const std::function<void(SourceSnapshot &)> &mutate) const
{
    QMutexLocker locker(&inner_->mutex);
    for (const std::unique_ptr<TaskState> &task : inner_->tasks) {
        if (task->snapshot.id != taskId) {
            continue;
        }
        auto found = task->sources.find(sourceId);
        if (found != task->sources.end()) {
            mutate(found->snapshot);
            found->snapshot.elapsedMs = quint64(found->started.elapsed());
        }
        return;
    }
}

void TaskTracker::finishSource(const QString &taskId, const QString &sourceId, bool ok,
                               const QString &text) const
{
    QMutexLocker locker(&inner_->mutex);
    for (const std::unique_ptr<TaskState> &task : inner_->tasks) {
        if (task->snapshot.id != taskId) {
            continue;
        }
        auto found = task->sources.find(sourceId);
        if (found == task->sources.end()) {
            return;
        }
        SourceSnapshot &source = found->snapshot;
        if (isTerminalPhase(source.phase)) {
            return;
        }
        source.phase = ok ? Phase::Completed : Phase::Failed;
        source.elapsedMs = quint64(found->started.elapsed());
        if (ok) {
            source.message = text;
            task->snapshot.successfulSources += 1;
        } else {
            source.error = text;
            task->snapshot.failedSources += 1;
        }
        task->snapshot.completedSources += 1;
        bool allSourcesFinished = true;
        for (const SourceState &state : task->sources) {
            if (!isTerminalPhase(state.snapshot.phase)) {
                allSourcesFinished = false;
                break;
            }
        }
        if (task->snapshot.completedSources == task->snapshot.totalSources || allSourcesFinished) {
            task->snapshot.elapsedMs = quint64(task->started.elapsed());
            task->snapshot.finishedAtNanos = TimeUtil::nowUtcNanos();
        }
        return;
    }
}

void TaskTracker::phase(const QString &taskId, const QString &sourceId, Phase phase)
{
    update(taskId, sourceId, [phase](SourceSnapshot &source) { source.phase = phase; });
}

void TaskTracker::timings(const QString &taskId, const QString &sourceId,
                          const std::optional<quint64> &downloadMs,
                          const std::optional<quint64> &parseMs,
                          const std::optional<quint64> &writeMs,
                          const std::optional<quint64> &speedtestMs,
                          const std::optional<quint64> &updateMs)
{
    update(taskId, sourceId, [&](SourceSnapshot &source) {
        if (downloadMs.has_value()) {
            source.downloadMs = downloadMs;
        }
        if (parseMs.has_value()) {
            source.parseMs = parseMs;
        }
        if (writeMs.has_value()) {
            source.writeMs = writeMs;
        }
        if (speedtestMs.has_value()) {
            source.speedtestMs = speedtestMs;
        }
        if (updateMs.has_value()) {
            source.updateMs = updateMs;
        }
    });
}

void TaskTracker::nodes(const QString &taskId, const QString &sourceId, size_t nodeCount)
{
    update(taskId, sourceId,
           [nodeCount](SourceSnapshot &source) { source.nodeCount = nodeCount; });
}

void TaskTracker::complete(const QString &taskId, const QString &sourceId, const QString &message)
{
    finishSource(taskId, sourceId, true, message);
}

void TaskTracker::fail(const QString &taskId, const QString &sourceId, const QString &error)
{
    finishSource(taskId, sourceId, false, error);
}

Snapshot TaskTracker::renderSnapshot(const TaskState &task)
{
    Snapshot value = task.snapshot;
    if (!value.finishedAtNanos.has_value()) {
        value.elapsedMs = quint64(task.started.elapsed());
    }
    for (auto it = task.sources.constBegin(); it != task.sources.constEnd(); ++it) {
        SourceSnapshot source = it->snapshot;
        if (!isTerminalPhase(source.phase)) {
            source.elapsedMs = quint64(it->started.elapsed());
        }
        value.sources.push_back(source);
    }
    return value;
}

QVector<Snapshot> TaskTracker::snapshots(size_t limit) const
{
    QMutexLocker locker(&inner_->mutex);
    QVector<Snapshot> out;
    const size_t take = std::clamp(limit, size_t(1), kMaxTaskHistory);
    for (size_t index = 0; index < inner_->tasks.size() && size_t(out.size()) < take; ++index) {
        out.push_back(renderSnapshot(*inner_->tasks[index]));
    }
    return out;
}

bool TaskTracker::isSourceActive(const QString &sourceId) const
{
    QMutexLocker locker(&inner_->mutex);
    for (const std::unique_ptr<TaskState> &task : inner_->tasks) {
        auto found = task->sources.find(sourceId);
        if (found != task->sources.end() && !isTerminalPhase(found->snapshot.phase)) {
            return true;
        }
    }
    return false;
}

bool TaskTracker::hasActiveFetch() const
{
    QMutexLocker locker(&inner_->mutex);
    for (const std::unique_ptr<TaskState> &task : inner_->tasks) {
        if (task->snapshot.kind == Kind::Fetch && !task->snapshot.finishedAtNanos.has_value()) {
            return true;
        }
    }
    return false;
}

}
