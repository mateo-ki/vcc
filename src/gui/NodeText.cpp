#include "gui/NodeText.h"

#include "gui/UiText.h"

namespace Gui {

QString latencyKernelLabel(const QString &kernel)
{
    if (kernel == QLatin1String("mihomo")) {
        return QStringLiteral("Mihomo");
    }
    if (kernel == QLatin1String("sing_box")) {
        return QStringLiteral("Sing-box");
    }
    if (kernel.isEmpty()) {
        return UiText::kernelNameFallback();
    }
    return kernel;
}

QString latencyRole(qint64 ms)
{
    if (ms < 200) {
        return QStringLiteral("Success");
    }
    if (ms < 800) {
        return QStringLiteral("Subtitle");
    }
    return QStringLiteral("Title");
}

LatencyDisplay latencyText(const Models::NodeSummary &node)
{
    // NodeSummary 上这两个是 QString（空 = Rust 的 None）。
    const bool isGoogleProxy = node.latencyTestKind == QLatin1String(Models::kGoogleProxyTestKind);
    if (node.latencyOk.has_value() && *node.latencyOk) {
        const qint64 ms = node.latencyMs.value_or(0);
        if (isGoogleProxy) {
            return {QStringLiteral("%1 ms %2").arg(ms).arg(latencyKernelLabel(node.latencyKernel)),
                    latencyRole(ms)};
        }
        return {QStringLiteral("%1 ms TCP").arg(ms), QStringLiteral("Subtitle")};
    }
    if (node.latencyOk.has_value()) {
        if (isGoogleProxy) {
            return {UiText::latencyProxyFailed(), QStringLiteral("Danger")};
        }
        return {UiText::latencyTcpFailed(), QStringLiteral("Subtitle")};
    }
    return {UiText::latencyUntested(), QStringLiteral("Subtitle")};
}

}
