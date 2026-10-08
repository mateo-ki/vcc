// 由 tools/gen-collector-cjk-codepoints.mjs 生成，请勿手改。
// 对应 Rust src/collector/mod.rs 里的用户可见文本。
#pragma once

#include <QString>
#include <QVector>

#include <initializer_list>

namespace Collector {
namespace Messages {

/// 具名占位符：marker 写 Rust 模板里的原样（含花括号），例如 "{error}"。
struct Arg {
    const char *marker;
    QString value;
};

/// Rust 的 `format!`：第 i 个 `{}` 按顺序换成 positional[i]，`{name}` 按 marker 替换。
/// 只扫一遍模板，插进去的值不会被二次求值（与 Rust 一致）。
QString format(const QString &text, const QVector<QString> &positional = {},
               std::initializer_list<Arg> named = {});

QString viaMirrorNote();
QString directCandidatePrefix();
QString candidateTaskFailed();
QString mirrorGateClosed();
QString hostGateClosed();
QString responseHeaderTimeout();
QString connectTimeoutFailed();
QString gistDiscoveryName();
QString gistPageNotModified();
QString gistPageFetchFailed();
QString gistPageNoGists();
QString gistPageAllSeen();
QString gistPageCap();
QString gistRegisterCap();
QString gistSearchUrlUnparsable();
QString gistScanComplete();
QString gistDiscoveryFinished();
QString gistPageHandled();
QString gistRemarkAuto();
QString gistRegisterFailed();
QString attemptsSuffix();
QString degradedStreakWarn();
QString recordOutcomeFailed();
QString subtaskExited();

} // namespace Messages
} // namespace Collector
