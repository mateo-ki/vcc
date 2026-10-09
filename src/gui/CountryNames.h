#pragma once

#include <QString>

namespace Gui {

/// 国家/地区码 → （国旗 emoji, 中文名），对应 Rust `widgets.rs::country_flag_and_name`。
/// 认不出来的一律给兜底那对（🌐 / 其它），与 Rust 的 `_ =>` 分支一致。
/// 表体由 tools/gen-country-flags.mjs 从 Rust 源码生成，不要手改 CountryNames.cpp。
struct CountryDisplay {
    QString flag;
    QString name;
};

CountryDisplay countryFlagAndName(const QString &code);

}
