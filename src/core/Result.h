#pragma once

#include <QString>

#include <optional>
#include <utility>

// 失败传播对应 Rust 的 anyhow::Result<T>：不用异常，因为错误要跨线程池与信号槽边界。
template <typename T>
class Result {
public:
    Result() = default;

    static Result withValue(T value)
    {
        Result result;
        result.value_ = std::move(value);
        return result;
    }

    static Result withError(QString error)
    {
        Result result;
        result.error_ = error.isEmpty() ? QStringLiteral("unknown error") : std::move(error);
        return result;
    }

    bool isOk() const { return value_.has_value(); }
    bool isErr() const { return !value_.has_value(); }

    const T &value() const { return *value_; }
    T &value() { return *value_; }
    T takeValue() { return std::move(*value_); }
    const T &operator*() const { return *value_; }
    T &operator*() { return *value_; }
    const T *operator->() const { return &*value_; }
    T *operator->() { return &*value_; }

    const QString &error() const { return error_; }

    /// 对应 Rust 的 `.unwrap_or_else(|_| fallback)`：出错时给一个替代值。
    T valueOr(T fallback) const
    {
        return isOk() ? *value_ : std::move(fallback);
    }

    template <typename U>
    Result<U> mapErrorAs() const
    {
        return Result<U>::withError(error_);
    }

private:
    std::optional<T> value_;
    QString error_;
};

/// 无返回值但可能失败的操作，对应 Rust 的 Result<()>。
class Status
{
public:
    Status() = default;

    static Status succeeded() { return Status(); }
    static Status failed(QString error)
    {
        Status status;
        status.error_ = error.isEmpty() ? QStringLiteral("unknown error") : std::move(error);
        return status;
    }

    bool isOk() const { return error_.isEmpty(); }
    bool isErr() const { return !error_.isEmpty(); }
    const QString &error() const { return error_; }

private:
    QString error_;
};
