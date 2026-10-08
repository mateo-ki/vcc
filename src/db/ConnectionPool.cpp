#include "db/ConnectionPool.h"

Result<std::shared_ptr<ConnectionPool>> ConnectionPool::create(const QString &path,
                                                               int maxConnections)
{
    if (maxConnections < 1) {
        maxConnections = 1;
    }
    std::shared_ptr<ConnectionPool> pool(new ConnectionPool());
    pool->path_ = path;
    for (int index = 0; index < maxConnections; ++index) {
        Result<std::unique_ptr<Connection>> opened = Connection::open(path);
        if (opened.isErr()) {
            return Result<std::shared_ptr<ConnectionPool>>::withError(opened.error());
        }
        pool->connections_.push_back(std::move(opened.takeValue()));
        pool->busy_.push_back(false);
    }
    return Result<std::shared_ptr<ConnectionPool>>::withValue(std::move(pool));
}

ConnectionPool::~ConnectionPool() = default;

ConnectionPool::Lease ConnectionPool::acquire()
{
    QMutexLocker locker(&mutex_);
    for (;;) {
        for (size_t index = 0; index < connections_.size(); ++index) {
            if (!busy_[index]) {
                busy_[index] = true;
                return Lease(this, connections_[index].get());
            }
        }
        available_.wait(&mutex_);
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
