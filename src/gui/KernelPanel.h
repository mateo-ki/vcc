#pragma once

#include <QList>
#include <QString>

#include "kernel/KernelManager.h"
#include "kernel/KernelRelease.h"
#include "proxy/MihomoConfig.h"

namespace Gui {

/// 内核表一行该显示什么（对应 Rust `ui_proxy_kernels` 循环体里的各分支）。
/// 文案一律取 UiText，颜色只带"该用哪个语义色"的布尔，让 widget 层去对 QSS 角色。
struct KernelRow {
    Kernel::Kind kind = Kernel::Kind::Mihomo;
    QString name;
    QString statusText;
    bool statusOk = false;
    QString version;
    QString latest;
    bool latestIsWarning = false;
    QString path;
    QString actionText;
    bool actionEnabled = false;
    QString hover;
};

/// releases 里可能缺某个内核（还没检查完），缺的那行最新版本一律显示"检查中"。
QList<KernelRow> buildKernelRows(const QList<Kernel::Info> &kernels,
                                 const QList<Kernel::ReleaseInfo> &releases, bool busy,
                                 const Proxy::ProxyRuntimeStatus &status);

/// 进度条文字：`内核名 · 阶段 · 详情`，下载阶段把详情换成 `已下 / 总量 · 速率/s`，
/// 其它阶段直接用 progress.message。
QString kernelProgressText(const Kernel::UpdateProgress &progress);

}
