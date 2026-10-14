#pragma once

#include <QString>

#include "core/Result.h"

namespace Core {

/// 编译期由 CMake 的 project(Vcc VERSION x.y.z) 注入，安装包与程序内看到的是同一个数。
QString appVersion();

/// 发布 tag 与版本号是同一个东西的两种写法："v1.2.0" 与 "1.2.0"。
QString releaseVersion(const QString &tag);

/// 逐段数值比较，缺的段按 0 补：1.10.0 比 1.9.9 新，1.2 与 1.2.0 相等。
/// 不是数字的段按 0 处理，因为发布渠道上真出现的多半是 "v1.2.0-beta" 这类尾巴，
/// 我们要的是"能不能比出主版本号"，不是完整 semver 预发布顺序。
int compareVersions(const QString &left, const QString &right);

/// latest 比 current 新才谈得上"有更新"。
bool isUpdateAvailable(const QString &latestTag, const QString &currentVersion);

/// GitHub Releases/latest 里我们真正要的两件事：版本号与发布页地址。
struct Release {
    QString version;
    QString url;
};

Result<Release> releaseFromJsonText(const QString &jsonText);

} // namespace Core
