#pragma once

#include <QVector>
#include <QtConcurrent>

#include "core/Result.h"

/// 跨分库并发执行同一类查询再合并：串行 await 35 个分库是启动 scrub 和首页慢的主因。
/// 任一分支失败就用第一个错误向上抛，与 Rust 侧 `join.map_err(...)?` 一致。
template <typename T, typename Fn>
Result<QVector<T>> runOverScopes(const QVector<QString> &scopes, Fn function)
{
    if (scopes.isEmpty()) {
        return Result<QVector<T>>::withValue(QVector<T>());
    }
    QVector<QFuture<Result<QVector<T>>>> futures;
    futures.reserve(scopes.size());
    for (const QString &scope : scopes) {
        futures.append(QtConcurrent::run(function, scope));
    }
    QVector<T> merged;
    for (QFuture<Result<QVector<T>>> &future : futures) {
        future.waitForFinished();
        const Result<QVector<T>> result = future.result();
        if (result.isErr()) {
            return Result<QVector<T>>::withError(result.error());
        }
        merged.append(result.value());
    }
    return Result<QVector<T>>::withValue(std::move(merged));
}
