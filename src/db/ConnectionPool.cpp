#include "db/ConnectionPool.h"

Result<std::shared_ptr<ConnectionPool>> ConnectionPool::create(const QString &path,
                                                               int maxConnections,
                                                               qint64 idleTimeoutMs)
{
    if (maxConnections < 1) {
        maxConnections = 1;
    }
    std::shared_ptr<ConnectionPool> pool(new ConnectionPool());
    pool->path_ = path;
    pool->idleTimeoutMs_ = idleTimeoutMs > 0 ? idleTimeoutMs : 0;
    pool->idleClock_.start();
    for (int index = 0; index < maxConnections; ++index) {
        Result<std::unique_ptr<Connection>> opened = Connection::open(path);
        if (opened.isErr()) {
            return Result<std::shared_ptr<ConnectionPool>>::withError(opened.error());
        }
        pool->connections_.push_back(std::move(opened.takeValue()));
        pool->busy_.push_back(false);
        pool->shrunk_.push_back(false);
        // 刚开的连接也算"刚用过"，不然创建后第一次回收会立刻去缩它。
        // 必须是当前时刻：0 是"时钟起点"，慢机器上建库+建表一旦超过 idle 线，这条没被
        // 用过的连接就会在第一次 reap 时被缩掉（CI 上实测 45ms 就翻车）。
        pool->lastUsedMs_.push_back(pool->idleClock_.elapsed());
    }
    return Result<std::shared_ptr<ConnectionPool>>::withValue(std::move(pool));
}

ConnectionPool::~ConnectionPool() = default;

int ConnectionPool::shrunkConnections() const
{
    QMutexLocker locker(&mutex_);
    int shrunk = 0;
    for (const uint8_t flag : shrunk_) {
        if (flag) {
            ++shrunk;
        }
    }
    return shrunk;
}

ConnectionPool::Lease ConnectionPool::acquire()
{
    QMutexLocker locker(&mutex_);
    for (;;) {
        for (size_t index = 0; index < connections_.size(); ++index) {
            if (!busy_[index]) {
                busy_[index] = true;
                lastUsedMs_[index] = idleClock_.elapsed();
                // 又要开始用了：缓存会重新长起来，下次回收要再轮到最后一次使用之后。
                shrunk_[index] = false;
                return Lease(this, connections_[index].get());
            }
        }
        available_.wait(&mutex_);
    }
}

/// 空闲回收 = **不关句柄**，只把这条连接的页缓存缩回去（`PRAGMA shrink_memory`）。
/// 为什么不是"关掉连接"：`acquire()` 没有错误通道（Rust 那边是 `Result`），而且本仓库
/// 确实存在同一个池嵌套借两条连接的调用形态 —— 一旦把槽关掉、只留 1 条，第二个借用者
/// 就会永远等在 `available_.wait()` 上（我自己写的测试当场就把这个死锁跑出来了）。
/// 句柄留着、缓存缩掉，既拿到内存又不改并发语义。
void ConnectionPool::reapIdle()
{
    if (idleTimeoutMs_ <= 0) {
        return;
    }
    QMutexLocker locker(&mutex_);
    const qint64 now = idleClock_.elapsed();
    for (size_t index = 0; index < connections_.size(); ++index) {
        if (busy_[index] || shrunk_[index] || !connections_[index]) {
            continue;
        }
        if (now - lastUsedMs_[index] > idleTimeoutMs_) {
            if (connections_[index]->execute(QStringLiteral("PRAGMA shrink_memory")).isOk()) {
                shrunk_[index] = true;
            }
        }
    }
}

void ConnectionPool::Lease::release()
{
    if (!pool_ || !connection_) {
        return;
    }
    ConnectionPool *pool = pool_;
    QMutexLocker locker(&pool->mutex_);
    for (size_t index = 0; index < pool->connections_.size(); ++index) {
        if (pool->connections_[index].get() == connection_) {
            pool->busy_[index] = false;
            pool->lastUsedMs_[index] = pool->idleClock_.elapsed();
            break;
        }
    }
    pool->available_.wakeOne();
    pool_ = nullptr;
    connection_ = nullptr;
}

ConnectionPool::Lease::~Lease()
{
    release();
}

ConnectionPool::Lease::Lease(Lease &&other) noexcept : pool_(other.pool_),
                                                       connection_(other.connection_)
{
    other.pool_ = nullptr;
    other.connection_ = nullptr;
}

ConnectionPool::Lease &ConnectionPool::Lease::operator=(Lease &&other) noexcept
{
    if (this != &other) {
        release();
        pool_ = other.pool_;
        connection_ = other.connection_;
        other.pool_ = nullptr;
        other.connection_ = nullptr;
    }
    return *this;
}

Transaction::Transaction(ConnectionPool::Lease lease) : lease_(std::move(lease))
{
    if (lease_.isValid()) {
        const Status status = lease_->execute(QStringLiteral("BEGIN"));
        finished_ = status.isErr();
    }
}

Transaction::Transaction(Transaction &&other) noexcept
    : lease_(std::move(other.lease_)), finished_(other.finished_)
{
    other.finished_ = true;
}

Transaction &Transaction::operator=(Transaction &&other) noexcept
{
    if (this != &other) {
        if (!finished_) {
            rollback();
        }
        lease_ = std::move(other.lease_);
        finished_ = other.finished_;
        other.finished_ = true;
    }
    return *this;
}

Transaction::~Transaction()
{
    if (!finished_) {
        rollback();
    }
}

Status Transaction::commit()
{
    if (!lease_.isValid() || finished_) {
        return Status::failed(QStringLiteral("事务不可用"));
    }
    const Status status = lease_->execute(QStringLiteral("COMMIT"));
    finished_ = true;
    return status;
}

void Transaction::rollback()
{
    if (!lease_.isValid() || finished_) {
        return;
    }
    lease_->execute(QStringLiteral("ROLLBACK"));
    finished_ = true;
}
