#pragma once

#include <QHash>
#include <QString>

#include <memory>
#include <optional>

#include "core/Result.h"

struct sqlite3;
struct sqlite3_stmt;

/// 预处理语句：绑定 -> 逐行 step -> 析构时释放。
class Statement {
public:
    friend class Connection;

    Statement() = default;
    ~Statement();
    Statement(Statement &&other) noexcept;
    Statement &operator=(Statement &&other) noexcept;
    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;

    void bindNull(int index);
    void bindText(int index, const QString &value);
    void bindInt64(int index, qint64 value);
    void bindDouble(int index, double value);
    void bindBool(int index, bool value);

    /// true=有一行, false=执行结束。
    Result<bool> step();
    Status reset();

    bool isNull(int column) const;
    /// 列值为 SQL NULL 时返回 null QString（与空串区分）。
    QString text(int column) const;
    qint64 int64(int column) const;
    double doubleValue(int column) const;

    // 按列名取值：对应 sqlx 的 `row.get("column")`，索引映射只做一次。
    int indexOf(const char *columnName) const;
    bool isNull(const char *columnName) const { return isNull(indexOf(columnName)); }
    QString text(const char *columnName) const { return text(indexOf(columnName)); }
    qint64 int64(const char *columnName) const { return int64(indexOf(columnName)); }
    double doubleValue(const char *columnName) const { return doubleValue(indexOf(columnName)); }
    /// SQL 里的 0/1 整数列；NULL 时返回 fallback。
    bool flag(const char *columnName, bool fallback = false) const
    {
        const int index = indexOf(columnName);
        return isNull(index) ? fallback : int64(index) != 0;
    }
    std::optional<qint64> optionalInt64(const char *columnName) const
    {
        const int index = indexOf(columnName);
        return isNull(index) ? std::nullopt : std::optional<qint64>(int64(index));
    }
    std::optional<double> optionalDouble(const char *columnName) const
    {
        const int index = indexOf(columnName);
        return isNull(index) ? std::nullopt : std::optional<double>(doubleValue(index));
    }
    std::optional<bool> optionalBool(const char *columnName) const
    {
        const int index = indexOf(columnName);
        return isNull(index) ? std::nullopt : std::optional<bool>(int64(index) != 0);
    }

    bool isValid() const { return statement_ != nullptr; }

private:
    explicit Statement(sqlite3_stmt *statement) : statement_(statement) {}
    void release();
    void primeColumnIndex() const;

    sqlite3_stmt *statement_ = nullptr;
    mutable QHash<QByteArray, int> columnIndex_;
    mutable bool columnIndexPrimed_ = false;
};

/// 一条打开的 SQLite 连接。参数与 Rust 侧 sqlx 完全对齐：WAL、synchronous=NORMAL、
/// busy_timeout=10s、cache_size=-2000。
class Connection {
public:
    static Result<std::unique_ptr<Connection>> open(const QString &path);
    ~Connection();

    Connection(Connection &&other) noexcept;
    Connection &operator=(Connection &&other) noexcept;
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

    /// 整段执行（可含多条语句），失败返回错误文本。
    Status execute(const QString &sql);
    /// 忽略错误的执行，对应 Rust 侧 `let _ = sqlx::query(...)` 的迁移语句。
    void executeIgnoringFailure(const QString &sql);

    /// 含 ? 占位符的语句用这个。
    Result<Statement> prepare(const QString &sql);

    qint64 changes() const;
    qint64 lastInsertRowId() const;
    QString errorMessage() const;
    QString path() const { return path_; }

private:
    Connection(sqlite3 *handle, QString path) : handle_(handle), path_(std::move(path)) {}

    sqlite3 *handle_ = nullptr;
    QString path_;
};
