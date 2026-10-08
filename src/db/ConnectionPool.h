#pragma once

#include <QMutex>
#include <QWaitCondition>

#include <memory>
#include <vector>

#include "core/Result.h"
#include "db/Sqlite.h"

/// 对应 sqlx 的 SqlitePool：固定上限的连接集合，取满就阻塞等待。
/// 保留多连接是为了 WAL 下"有独占写者时读不被卡住"这条性质。
class ConnectionPool {
public:
    static Result<std::shared_ptr<ConnectionPool>> create(const QString &path, int maxConnections);

    ~ConnectionPool();

    QString path() const { return path_; }
    int maxConnections() const { return int(connections_.size()); }

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
    std::vector<bool> busy_;
    QMutex mutex_;
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
