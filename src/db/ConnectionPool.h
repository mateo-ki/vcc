#pragma once

#include <QElapsedTimer>
#include <QMutex>
#include <QWaitCondition>

#include <cstdint>
#include <memory>
#include <vector>

#include "core/Result.h"
#include "db/Sqlite.h"

/// 对应 sqlx 的 SqlitePool：固定上限的连接集合，取满就阻塞等待。
/// 保留多连接是为了 WAL 下"有独占写者时读不被卡住"这条性质。
class ConnectionPool {
public:
    /// `idleTimeoutMs = 0` 表示不回收（旧行为）。Rust 侧两条池都设了 `idle_timeout`
    /// （主库 60s、订阅分库 30s），空闲够久 sqlx 会把连接关掉、要用再开。这里做同一件事的
    /// **安全版本**：句柄保留（本仓库有"同池嵌套借两条"的调用形态，关槽会死锁），只把每条
    /// 连接 ~2MB 的页缓存缩回去。34 源 × 2 条不处理就是实测那 36MB，所以是补齐不是优化。
    static Result<std::shared_ptr<ConnectionPool>> create(const QString &path, int maxConnections,
                                                          qint64 idleTimeoutMs = 0);

    ~ConnectionPool();

    QString path() const { return path_; }
    /// 配置上限（槽位数），不是"当前真开着的条数"。
    int maxConnections() const { return int(connections_.size()); }
    /// 当前已经"缩过页缓存但还没被再次使用"的连接条数（回收效果的观测量）。
    int shrunkConnections() const;
    /// 把空闲够久的连接的页缓存缩掉（`PRAGMA shrink_memory`，句柄保留）。
    void reapIdle();

    class Lease {
    public:
        Lease() = default;
        ~Lease();
        Lease(Lease &&other) noexcept;
        Lease &operator=(Lease &&other) noexcept;
        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;

        Connection *operator->() const { return connection_; }
        Connection &operator*() const { return *connection_; }
        bool isValid() const { return connection_ != nullptr; }

    private:
        Lease(ConnectionPool *pool, Connection *connection) : pool_(pool), connection_(connection) {}
        void release();

        friend class ConnectionPool;
        ConnectionPool *pool_ = nullptr;
        Connection *connection_ = nullptr;
    };

    Lease acquire();

private:
    ConnectionPool() = default;

    QString path_;
    std::vector<std::unique_ptr<Connection>> connections_;
    std::vector<uint8_t> busy_;
    /// 与 connections_ 平行：这条的连接池已经被 reapIdle 缩过、且之后没再被用过。
    std::vector<uint8_t> shrunk_;
    /// 与 connections_ 平行：最后一次"归还"的时刻（单调毫秒）。
    std::vector<qint64> lastUsedMs_;
    qint64 idleTimeoutMs_ = 0;
    QElapsedTimer idleClock_;
    mutable QMutex mutex_;
    QWaitCondition available_;
};

/// 事务：BEGIN/COMMIT，异常路径析构自动 ROLLBACK。
class Transaction {
public:
    Transaction() = default;
    explicit Transaction(ConnectionPool::Lease lease);
    Transaction(Transaction &&other) noexcept;
    Transaction &operator=(Transaction &&other) noexcept;
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;
    ~Transaction();

    Connection *connection() { return lease_.isValid() ? &*lease_ : nullptr; }
    Status commit();
    void rollback();

private:
    ConnectionPool::Lease lease_;
    bool finished_ = true;
};
