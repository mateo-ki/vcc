// 由 tools/gen-cjk-codepoints.mjs 生成，请勿手改。
#pragma once

#include <QString>

namespace Proxy::Messages {

QString unknownError();
QString selfReferentialWarning();
QString diagReset();
QString diagTimeout();
QString diagRefused();
QString diagTls();
QString diagWebsocket();
QString diagInitialConfig();
QString diagOutbound();
QString kernelDefaultName();
QString startingPrefix();
QString stoppingPrefix();
QString kernelExitedPrefix();
QString portNotReady();
QString launchFailedPrefix();
QString connectivityFailedPrefix();
QString degradedPrefix();
QString startupTimeoutUnverified();
QString startupTimeoutNoSystemProxy();
QString enableSystemProxyFailed();
QString runningPrefix();
QString initialConnecting();
QString switchFailedKeepProxy();
QString switchedPrefix();
QString hotReloaded();
QString switchElapsedPrefix();
QString tunNeedsAdmin();
QString busySwitching();
QString stopFailureJoiner();
QString verifyJoiner();
QString localProxyUnreachable();
QString verifyExceeded();
QString verifyTaskFailed();
QString unexpectedStatus204();
QString kernelNotFoundExecutable();
QString generateConfigFailed();
QString createWorkDirFailed();
QString writeConfigFailed();
QString createLogFailed();
QString openLogFailed();
QString spawnKernelFailed();
QString stopKernelFailed();
QString readKernelStateFailed();
QString waitKernelExitFailed();
QString cleanupWorkDirFailed();
QString restoreProxyFailed();
QString restoreAfterStopFailure();
QString portOccupied();
QString urlTestFallbackWarning();

}
