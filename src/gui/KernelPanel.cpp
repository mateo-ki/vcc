#include "gui/KernelPanel.h"

#include "gui/UiText.h"
#include "gui/Widgets.h"
#include "kernel/KernelTypes.h"
#include "sysproxy/Format.h"

namespace Gui {

QList<KernelRow> buildKernelRows(const QList<Kernel::Info> &kernels,
                                 const QList<Kernel::ReleaseInfo> &releases, bool busy,
                                 const Proxy::ProxyRuntimeStatus &status)
{
    QList<KernelRow> rows;
    for (const Kernel::Info &info : kernels) {
        KernelRow row;
        row.kind = info.kind;
        row.name = Kernel::kindDisplayName(info.kind);
        row.statusText = info.available ? UiText::kernelStateAvailable()
                                       : UiText::kernelStateMissing();
        row.statusOk = info.available;
        row.version = info.version.isEmpty() ? QStringLiteral("-") : info.version;
        row.path = info.path.isEmpty() ? QStringLiteral("-") : info.path;
        row.actionText = info.available ? UiText::kernelActionUpdate()
                                        : UiText::kernelActionDownload();
        row.hover = status.kernelInUse(info.kind) ? UiText::kernelHoverInUse()
                                                  : UiText::kernelHoverAsset();

        const Kernel::ReleaseInfo *release = nullptr;
        for (const Kernel::ReleaseInfo &candidate : releases) {
            if (candidate.kind == info.kind) {
                release = &candidate;
                break;
            }
        }
        if (release != nullptr && !release->latestVersion.isEmpty()) {
            row.latest = release->latestVersion;
        } else {
            row.latest = UiText::kernelLatestChecking();
        }
        const bool updateAvailable = release != nullptr && release->updateAvailable;
        row.latestIsWarning = updateAvailable;
        row.actionEnabled = updateAvailable && !busy && !status.kernelInUse(info.kind);
        rows.push_back(row);
    }
    return rows;
}

QString kernelProgressText(const Kernel::UpdateProgress &progress)
{
    QString detail = progress.message;
    if (progress.stage == Kernel::UpdateStage::Downloading) {
        detail = SysProxy::fillTemplate(
            UiText::kernelDownloadFormat(), {},
            {formatBytes(progress.downloaded), formatBytes(progress.total),
             formatBytes(static_cast<quint64>(progress.bytesPerSecond))});
    }
    return SysProxy::fillTemplate(UiText::kernelProgressFormat(), {},
                                  {Kernel::kindDisplayName(progress.kind),
                                   Kernel::updateStageLabel(progress.stage), detail});
}

}
