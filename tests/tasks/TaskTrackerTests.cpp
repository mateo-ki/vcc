#include <gtest/gtest.h>

#include <QThread>

#include <vector>

#include "tasks/TaskTracker.h"

namespace {

QVector<std::pair<QString, QString>> sources(std::vector<std::pair<const char *, const char *>> rows)
{
    QVector<std::pair<QString, QString>> out;
    for (const auto &row : rows) {
        out.push_back({QString::fromUtf8(row.first), QString::fromUtf8(row.second)});
    }
    return out;
}

/// 按值返回：早先写成返回 const 引用，那个 QVector 在语句结束就析构了。
Tasks::Snapshot only(Tasks::TaskTracker &tracker)
{
    const QVector<Tasks::Snapshot> snapshots = tracker.snapshots(1);
    EXPECT_EQ(snapshots.size(), 1);
    return snapshots.first();
}

}

TEST(TaskTracker, tracksSuccessAndFailure)
{
    Tasks::TaskTracker tracker;
    const QString id = tracker.start(Tasks::Kind::Fetch, sources({{"a", "A"}, {"b", "B"}}));
    tracker.phase(id, QStringLiteral("a"), Tasks::Phase::Downloading);
    tracker.complete(id, QStringLiteral("a"), QStringLiteral("ok"));
    tracker.fail(id, QStringLiteral("b"), QStringLiteral("broken"));

    const Tasks::Snapshot task = only(tracker);
    EXPECT_EQ(task.completedSources, 2u);
    EXPECT_EQ(task.successfulSources, 1u);
    EXPECT_EQ(task.failedSources, 1u);
    ASSERT_EQ(task.sources.size(), 2);
    // BTreeMap 顺序：source_id 升序，序列化进 /tasks 也是这个顺序。
    EXPECT_EQ(task.sources.at(0).sourceId.toStdString(), std::string("a"));
    EXPECT_EQ(task.sources.at(0).phase, Tasks::Phase::Completed);
    EXPECT_TRUE(task.sources.at(0).message.has_value());
    EXPECT_EQ(task.sources.at(1).error->toStdString(), std::string("broken"));
    EXPECT_TRUE(task.finishedAtNanos.has_value());
}

TEST(TaskTracker, finishedTaskAndSourceElapsedTimesStopGrowing)
{
    Tasks::TaskTracker tracker;
    const QString id = tracker.start(Tasks::Kind::Fetch, sources({{"a", "A"}}));
    tracker.fail(id, QStringLiteral("a"), QStringLiteral("broken"));
    const Tasks::Snapshot first = only(tracker);
    QThread::msleep(20);
    const Tasks::Snapshot second = only(tracker);

    EXPECT_EQ(first.elapsedMs, second.elapsedMs);
    EXPECT_EQ(first.sources.at(0).elapsedMs, second.sources.at(0).elapsedMs);
    EXPECT_TRUE(second.finishedAtNanos.has_value());
}

TEST(TaskTracker, activeSourceTrackingClearsAfterFailure)
{
    Tasks::TaskTracker tracker;
    const QString id = tracker.start(Tasks::Kind::Fetch, sources({{"a", "A"}}));
    EXPECT_TRUE(tracker.isSourceActive(QStringLiteral("a")));
    EXPECT_TRUE(tracker.hasActiveFetch());

    tracker.fail(id, QStringLiteral("a"), QStringLiteral("broken"));

    EXPECT_FALSE(tracker.isSourceActive(QStringLiteral("a")));
    EXPECT_FALSE(tracker.hasActiveFetch());
}

TEST(TaskTracker, snapshotsJsonMatchesSerdeFieldNamesAndNulls)
{
    Tasks::TaskTracker tracker;
    const QString id = tracker.start(Tasks::Kind::Speedtest, sources({{"s1", "Src1"}}));
    tracker.timings(id, QStringLiteral("s1"), quint64(12), std::nullopt, std::nullopt,
                    quint64(34), std::nullopt);
    tracker.nodes(id, QStringLiteral("s1"), 7);
    tracker.complete(id, QStringLiteral("s1"), QStringLiteral("done"));

    const Json json = only(tracker).toJson();
    // 键名与 Rust serde 输出一致，未填的计时字段必须是 null 而不是 0（界面据此区分"没测"）。
    EXPECT_EQ(json["kind"], "speedtest");
    EXPECT_EQ(json["sources"][0]["phase"], "completed");
    EXPECT_EQ(json["sources"][0]["download_ms"], 12);
    EXPECT_EQ(json["sources"][0]["parse_ms"], Json(nullptr));
    EXPECT_EQ(json["sources"][0]["speedtest_ms"], 34);
    EXPECT_EQ(json["sources"][0]["node_count"], 7);
    EXPECT_EQ(json["sources"][0]["error"], Json(nullptr));
    EXPECT_EQ(json["started_at"].get<std::string>().back(), 'Z');
}

TEST(TaskTracker, historyIsCappedAndNewestFirst)
{
    Tasks::TaskTracker tracker;
    for (size_t index = 0; index < Tasks::kMaxTaskHistory + 5; ++index) {
        tracker.start(Tasks::Kind::Fetch, sources({{"a", "A"}}));
    }
    const QVector<Tasks::Snapshot> snapshots = tracker.snapshots(1000);
    EXPECT_EQ(snapshots.size(), Tasks::kMaxTaskHistory);
    // limit 会被夹到 [1, 20]，0 也要给一条，而不是空列表。
    EXPECT_EQ(tracker.snapshots(0).size(), 1);
}

TEST(TaskTracker, finishIsIdempotentAfterTerminalPhase)
{
    Tasks::TaskTracker tracker;
    const QString id = tracker.start(Tasks::Kind::Fetch, sources({{"a", "A"}, {"b", "B"}}));
    tracker.complete(id, QStringLiteral("a"), QStringLiteral("ok"));
    tracker.complete(id, QStringLiteral("a"), QStringLiteral("again"));
    tracker.fail(id, QStringLiteral("a"), QStringLiteral("late failure"));

    const Tasks::Snapshot task = only(tracker);
    EXPECT_EQ(task.successfulSources, 1u);
    EXPECT_EQ(task.failedSources, 0u);
    EXPECT_EQ(task.completedSources, 1u);
    EXPECT_EQ(task.sources.at(0).message->toStdString(), std::string("ok"));
    EXPECT_FALSE(task.finishedAtNanos.has_value());
}
