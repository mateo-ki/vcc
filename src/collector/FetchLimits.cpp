#include "collector/FetchLimits.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>

#include "collector/Collector.h" // urlHost：collector/mod.rs:1011
#include "parser/Common.h"       // toAsciiLower：只动 A-Z，与 Rust 的 to_ascii_lowercase 同

namespace Collector {

Semaphore::Semaphore(int permits)
    : permits_(qMax(0, permits))
{
}

void Semaphore::acquire()
{
    std::unique_lock<std::mutex> lock(mutex_);
    // 不写成 `cv.wait(pred)` 之外的自旋：虚假唤醒时再次检查 permits_。
    condition_.wait(lock, [this] { return permits_ > 0; });
    --permits_;
}

bool Semaphore::tryAcquire()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (permits_ <= 0) {
        return false;
    }
    --permits_;
    return true;
}

void Semaphore::release()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++permits_;
    }
    condition_.notify_one();
}

int Semaphore::available()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return permits_;
}

Semaphore::Guard::Guard(SemaphorePtr semaphore)
    : semaphore_(std::move(semaphore))
{
    if (semaphore_) {
        semaphore_->acquire();
    }
}

Semaphore::Guard::Guard(Guard &&other) noexcept
    : semaphore_(std::move(other.semaphore_))
{
}

Semaphore::Guard &Semaphore::Guard::operator=(Guard &&other) noexcept
{
    if (this != &other) {
        if (semaphore_) {
            semaphore_->release();
        }
        semaphore_ = std::move(other.semaphore_);
    }
    return *this;
}

Semaphore::Guard::~Guard()
{
    if (semaphore_) {
        semaphore_->release();
    }
}

Semaphore::Guard Semaphore::acquireGuard()
{
    return Guard(shared_from_this());
}

namespace {

/// 进程级三个闸：Rust 侧是 `static Lazy<Arc<Semaphore>>`（:38、:78）。
struct StaticLimits {
    std::mutex mutex;
    SemaphorePtr mirror;
    SemaphorePtr parse;
};

StaticLimits &statics()
{
    static StaticLimits values;
    return values;
}

/// 按主机闸注册表（HOST_FETCH_LIMITS，:39）。弱引用：所有许可都归还、Arc 也散了之后
/// 这个主机的闸就没了，下次重新按 PER_HOST_FETCH_CONCURRENCY 建一个（Rust 同语义）。
struct HostRegistry {
    QMutex mutex;
    QHash<QString, std::weak_ptr<Semaphore>> limits;
};

HostRegistry &registry()
{
    static HostRegistry value;
    return value;
}

} // namespace

SemaphorePtr mirrorLimit()
{
    StaticLimits &values = statics();
    std::lock_guard<std::mutex> lock(values.mutex);
    if (!values.mirror) {
        // MIRROR_LIMIT = 6（:78）
        values.mirror = std::make_shared<Semaphore>(6);
    }
    return values.mirror;
}

SemaphorePtr parseLimit()
{
    StaticLimits &values = statics();
    std::lock_guard<std::mutex> lock(values.mutex);
    if (!values.parse) {
        // PARSE_LIMIT = 4（:38）
        values.parse = std::make_shared<Semaphore>(4);
    }
    return values.parse;
}

QString hostKey(const QString &url)
{
    // Rust: Url::parse(url).ok().and_then(|u| u.host_str().map(to_ascii_lowercase))
    //       .unwrap_or_else(|| url.to_ascii_lowercase())
    const QString host = urlHost(url);
    return host.isNull() ? Parser::Common::toAsciiLower(url) : host;
}

SemaphorePtr hostLimit(const QString &url)
{
    const QString key = hostKey(url);
    HostRegistry &table = registry();
    SemaphorePtr created;
    {
        // 只在这把锁里查表/建表，acquire 一定在锁外做（QMutex 不可重入，
        // 而且抱着注册表去等许可会把别的主机也堵住）。
        QMutexLocker lock(&table.mutex);
        for (auto it = table.limits.begin(); it != table.limits.end();) {
            if (it.value().use_count() == 0) {
                it = table.limits.erase(it); // 对应 limits.retain(strong_count > 0)
                continue;
            }
            ++it;
        }
        const auto existing = table.limits.constFind(key);
        if (existing != table.limits.constEnd()) {
            SemaphorePtr upgraded = existing.value().lock();
            if (upgraded) {
                return upgraded;
            }
        }
        // PER_HOST_FETCH_CONCURRENCY = 2（:41）
        created = std::make_shared<Semaphore>(2);
        table.limits.insert(key, created);
    }
    return created;
}

int liveHostLimitCount()
{
    HostRegistry &table = registry();
    QMutexLocker lock(&table.mutex);
    int live = 0;
    for (const std::weak_ptr<Semaphore> &weak : table.limits) {
        if (weak.use_count() > 0) {
            ++live;
        }
    }
    return live;
}

} // namespace Collector
