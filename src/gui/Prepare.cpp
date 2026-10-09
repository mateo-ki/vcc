#include "gui/Prepare.h"

namespace Gui {
namespace Prepare {

bool isPrepared(bool hasKernel, bool proxyActive)
{
    return !hasKernel && !proxyActive;
}

bool shouldDownloadKernel(bool enabled, bool hasKernel, bool proxyActive, bool kernelBusy,
                          int attempts, std::optional<qint64> sinceLastMs)
{
    if (!enabled || !isPrepared(hasKernel, proxyActive) || kernelBusy) {
        return false;
    }
    if (attempts == 0) {
        return true;
    }
    return attempts < kMaxAutoAttempts
        && sinceLastMs.has_value() && *sinceLastMs >= kMinAttemptSpacingMs;
}

bool shouldTestAddedSource(bool enabled, bool pendingForThis, bool fetchSucceeded,
                           qint64 nodeCount)
{
    return enabled && pendingForThis && fetchSucceeded && nodeCount > 0;
}

}
}
