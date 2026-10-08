// 由 tools/gen-platform-strings.mjs 生成，请勿手改。
// 模板里保留 Rust 的 `{}` / `{:?}` / `{name}` 占位符，由 platform/Format.h 的 fillTemplate 填充。
#pragma once

#include <QString>

namespace Platform::Messages {

QString menuShow();
QString menuStopProxy();
QString menuStartProxy();
QString menuExit();
QString trayTooltip();
QString revealNoWindow();
QString revealPosted();
QString elevationFailed();
QString tunNotSupported();
QString orphanCoreKilled();
QString ruleNameTemplate();
QString netshFailed();
QString canonicalizeFailed();
QString addRuleFailed();
QString unknownError();
QString alreadyAllowed();
QString addedToAllowList();
QString allReady();
QString partialFailure();
QString failureJoiner();
QString windowsOnlySingle();
QString windowsOnlyMany();

}
