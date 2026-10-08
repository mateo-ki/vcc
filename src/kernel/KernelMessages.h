// 由 tools/gen-kernel-cjk-codepoints.mjs 生成，请勿手改。
// 对应 Rust src/kernel/mod.rs 与 src/kernel/updater.rs 里的用户可见文本。
#pragma once

#include <QString>

namespace Kernel::Messages {

QString unsupportedProtocol();
QString noInstalledKernelSupporting();
QString xrayNoTunMode();
QString kernelDoesNotSupportProtocol();
QString kernelExecutableNotFound();
QString availableWithoutVersion();
QString kernelNotFoundMessage();
QString installedButCannotRun();
QString probeSucceededWithoutOutput();
QString probeIllegalInstruction();
QString probeAccessViolation();
QString probeStackOverflow();
QString probeKnownTail();
QString probeCannotExecute();
QString readVersionTimeout();
QString executeKernelFailed();
QString checkUpdateFailed();
QString stageChecking();
QString stageDownloading();
QString stageVerifying();
QString stageExtracting();
QString stageInstalling();
QString sourceGhProxyLabel();
QString sourceGhProxyHint();
QString sourceGhFastLabel();
QString sourceGhFastHint();
QString sourceOfficialLabel();
QString sourceOfficialHint();
QString sourceProxyLabel();
QString sourceProxyHint();
QString sourceLocalLabel();
QString sourceLocalHint();
QString sourceLabelFallback();
QString directTransport();

}
