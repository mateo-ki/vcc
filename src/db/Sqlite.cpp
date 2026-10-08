#include "db/Sqlite.h"

#include <sqlite3.h>

namespace {

QString lastError(sqlite3 *handle)
{
    const char *message = sqlite3_errmsg(handle);
    return message ? QString::fromUtf8(message) : QStringLiteral("未知 SQLite 错误");
}

QString statementError(sqlite3_stmt *statement)
{
    const char *message = sqlite3_errmsg(sqlite3_db_handle(statement));
    return message ? QString::fromUtf8(message) : QStringLiteral("未知 SQLite 错误");
}

}

Statement::~Statement()
{
    release();
}

Statement::Statement(Statement &&other) noexcept : statement_(other.statement_)
{
    other.statement_ = nullptr;
}

Statement &Statement::operator=(Statement &&other) noexcept
{
    if (this != &other) {
        release();
        statement_ = other.statement_;
        other.statement_ = nullptr;
    }
    return *this;
}

void Statement::release()
{
    if (statement_) {
        sqlite3_finalize(statement_);
        statement_ = nullptr;
    }
}

void Statement::bindNull(int index)
{
    if (statement_) {
        sqlite3_bind_null(statement_, index);
    }
}

void Statement::bindText(int index, const QString &value)
{
    if (!statement_) {
        return;
    }
    if (value.isNull()) {
        sqlite3_bind_null(statement_, index);
        return;
    }
    const QByteArray utf8 = value.toUtf8();
    sqlite3_bind_text(statement_, index, utf8.constData(), int(utf8.size()), SQLITE_TRANSIENT);
}

void Statement::bindInt64(int index, qint64 value)
{
    if (statement_) {
        sqlite3_bind_int64(statement_, index, sqlite3_int64(value));
    }
}

void Statement::bindDouble(int index, double value)
{
    if (statement_) {
        sqlite3_bind_double(statement_, index, value);
    }
}

void Statement::bindBool(int index, bool value)
{
    if (statement_) {
        sqlite3_bind_int(statement_, index, value ? 1 : 0);
    }
}

Result<bool> Statement::step()
{
    if (!statement_) {
        return Result<bool>::withError(QStringLiteral("语句已失效"));
    }
    const int code = sqlite3_step(statement_);
    if (code == SQLITE_ROW) {
        return Result<bool>::withValue(true);
    }
    if (code == SQLITE_DONE) {
        return Result<bool>::withValue(false);
    }
    return Result<bool>::withError(statementError(statement_));
}

Status Statement::reset()
{
    if (!statement_) {
        return Status::failed(QStringLiteral("语句已失效"));
    }
    sqlite3_reset(statement_);
    const int code = sqlite3_clear_bindings(statement_);
    if (code != SQLITE_OK) {
        return Status::failed(statementError(statement_));
    }
    return Status::succeeded();
}

bool Statement::isNull(int column) const
{
    if (column < 0 || !statement_) {
        return true;
    }
    return sqlite3_column_type(statement_, column) == SQLITE_NULL;
}

QString Statement::text(int column) const
{
    if (column < 0 || !statement_ || sqlite3_column_type(statement_, column) == SQLITE_NULL) {
        return QString();
    }
    const unsigned char *raw = sqlite3_column_text(statement_, column);
    if (!raw) {
        return QString();
    }
    const int bytes = sqlite3_column_bytes(statement_, column);
    return QString::fromUtf8(reinterpret_cast<const char *>(raw), bytes);
}

qint64 Statement::int64(int column) const
{
    if (column < 0 || !statement_) {
        return 0;
    }
    return qint64(sqlite3_column_int64(statement_, column));
}

double Statement::doubleValue(int column) const
{
    if (column < 0 || !statement_) {
        return 0.0;
    }
    return sqlite3_column_double(statement_, column);
}

void Statement::primeColumnIndex() const
{
    if (columnIndexPrimed_ || !statement_) {
        return;
    }
    const int count = sqlite3_column_count(statement_);
    for (int index = 0; index < count; ++index) {
        const char *name = sqlite3_column_name(statement_, index);
        if (name) {
            columnIndex_.insert(QByteArray(name), index);
        }
    }
    columnIndexPrimed_ = true;
}

int Statement::indexOf(const char *columnName) const
{
    primeColumnIndex();
    const auto found = columnIndex_.constFind(QByteArray(columnName));
    return found == columnIndex_.constEnd() ? -1 : found.value();
}

Connection::~Connection()
{
    if (handle_) {
        sqlite3_close(handle_);
        handle_ = nullptr;
    }
}

Connection::Connection(Connection &&other) noexcept : handle_(other.handle_), path_(std::move(other.path_))
{
    other.handle_ = nullptr;
}

Connection &Connection::operator=(Connection &&other) noexcept
{
    if (this != &other) {
        if (handle_) {
            sqlite3_close(handle_);
        }
        handle_ = other.handle_;
        path_ = std::move(other.path_);
        other.handle_ = nullptr;
    }
    return *this;
}

Result<std::unique_ptr<Connection>> Connection::open(const QString &path)
{
    sqlite3 *handle = nullptr;
    // 中文路径必须走 UTF-16 入口，sqlite3_open 的 char* 版本在 MSVC 下按本地代码页解释。
    const int code = sqlite3_open16(path.utf16(), &handle);
    if (code != SQLITE_OK) {
        const QString message = handle ? lastError(handle) : QStringLiteral("无法打开数据库");
        if (handle) {
            sqlite3_close(handle);
        }
        return Result<std::unique_ptr<Connection>>::withError(
            QStringLiteral("打开 %1 失败: %2").arg(path, message));
    }

    auto connection = std::unique_ptr<Connection>(new Connection(handle, path));
    for (const char *pragma : {"PRAGMA journal_mode=WAL",
                               "PRAGMA synchronous=NORMAL",
                               "PRAGMA busy_timeout=10000",
                               "PRAGMA cache_size=-2000"}) {
        const Status status = connection->execute(QLatin1String(pragma));
        if (status.isErr()) {
            return Result<std::unique_ptr<Connection>>::withError(
                QStringLiteral("%1 失败: %2").arg(QString::fromLatin1(pragma), status.error()));
        }
    }
    return Result<std::unique_ptr<Connection>>::withValue(std::move(connection));
}

Status Connection::execute(const QString &sql)
{
    if (!handle_) {
        return Status::failed(QStringLiteral("连接已关闭"));
    }
    char *errorMessage = nullptr;
    const int code = sqlite3_exec(handle_, sql.toUtf8().constData(), nullptr, nullptr,
                                  &errorMessage);
    if (code != SQLITE_OK) {
        const QString message = errorMessage ? QString::fromUtf8(errorMessage) : lastError(handle_);
        sqlite3_free(errorMessage);
        return Status::failed(QStringLiteral("%1（SQL: %2）").arg(message, sql));
    }
    sqlite3_free(errorMessage);
    return Status::succeeded();
}

void Connection::executeIgnoringFailure(const QString &sql)
{
    execute(sql);
}

Result<Statement> Connection::prepare(const QString &sql)
{
    if (!handle_) {
        return Result<Statement>::withError(QStringLiteral("连接已关闭"));
    }
    sqlite3_stmt *statement = nullptr;
    const QByteArray utf8 = sql.toUtf8();
    const int code = sqlite3_prepare_v2(handle_, utf8.constData(), int(utf8.size()), &statement,
                                        nullptr);
    if (code != SQLITE_OK || !statement) {
        if (statement) {
            sqlite3_finalize(statement);
        }
        return Result<Statement>::withError(
            QStringLiteral("%1（SQL: %2）").arg(lastError(handle_), sql));
    }
    return Result<Statement>::withValue(Statement(statement));
}

qint64 Connection::changes() const
{
    return handle_ ? qint64(sqlite3_changes(handle_)) : 0;
}

qint64 Connection::lastInsertRowId() const
{
    return handle_ ? qint64(sqlite3_last_insert_rowid(handle_)) : 0;
}

QString Connection::errorMessage() const
{
    return handle_ ? lastError(handle_) : QStringLiteral("连接已关闭");
}
