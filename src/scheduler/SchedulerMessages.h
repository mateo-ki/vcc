// 由 tests/speedtest/gen-speedtest-cjk.mjs 从 Rust 源生成，请勿手改。
// 对应 Rust src/scheduler/mod.rs 里写进日志的文本。
#pragma once

#include <QString>
#include <QVector>

#include <initializer_list>

namespace Scheduler {
namespace Messages {

/// 具名占位符：marker 写 Rust 模板里的原样（含花括号），例如 "{detail}"。
struct Arg {
    const char *marker;
    QString value;
};

// Rust 的 format!：第 i 个 `{}` 按顺序换成 positional[i]，`{name}` 按 marker 替换。
// 只扫一遍模板，插进去的值不会被二次求值（与 Rust 一致）。

QString format(const QString &text, const QVector<QString> &positional = {},
               std::initializer_list<Arg> named = {});

QString refillSparseLog();
QString refillDoneLog();
QString refillFailedLog();
QString refillCountFailedLog();

}
}
