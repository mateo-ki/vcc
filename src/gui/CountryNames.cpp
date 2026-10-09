// 由 tools/gen-country-flags.mjs 从 vpn-parser 的 gui/widgets.rs 生成，请勿手改。
// 国旗是区域指示符代理对（U+1F1E6..U+1F1FF），中文名与 emoji 都不允许手打转录。
#include "gui/CountryNames.h"

#include <QString>

#include <initializer_list>
#include <vector>

namespace {

using CodePoints = std::vector<char32_t>;

CodePoints cp(std::initializer_list<char32_t> points)
{
    return CodePoints(points);
}

QString fromCodePoints(const CodePoints &points)
{
    return QString::fromUcs4(points.data(), qsizetype(points.size()));
}

struct Row {
    std::vector<const char *> codes;
    CodePoints flag;
    CodePoints name;
};

const std::vector<Row> &rows()
{
    static const std::vector<Row> kRows = {
        { { "HK" },
          cp({ 0x1F1ED, 0x1F1F0 }),
          cp({ 0x9999, 0x6E2F }),
        },
        { { "JP" },
          cp({ 0x1F1EF, 0x1F1F5 }),
          cp({ 0x65E5, 0x672C }),
        },
        { { "US" },
          cp({ 0x1F1FA, 0x1F1F8 }),
          cp({ 0x7F8E, 0x56FD }),
        },
        { { "SG" },
          cp({ 0x1F1F8, 0x1F1EC }),
          cp({ 0x65B0, 0x52A0, 0x5761 }),
        },
        { { "TW" },
          cp({ 0x1F1E8, 0x1F1F3 }),
          cp({ 0x53F0, 0x6E7E }),
        },
        { { "KR" },
          cp({ 0x1F1F0, 0x1F1F7 }),
          cp({ 0x97E9, 0x56FD }),
        },
        { { "GB", "UK" },
          cp({ 0x1F1EC, 0x1F1E7 }),
          cp({ 0x82F1, 0x56FD }),
        },
        { { "DE" },
          cp({ 0x1F1E9, 0x1F1EA }),
          cp({ 0x5FB7, 0x56FD }),
        },
        { { "FR" },
          cp({ 0x1F1EB, 0x1F1F7 }),
          cp({ 0x6CD5, 0x56FD }),
        },
        { { "CA" },
          cp({ 0x1F1E8, 0x1F1E6 }),
          cp({ 0x52A0, 0x62FF, 0x5927 }),
        },
        { { "AU" },
          cp({ 0x1F1E6, 0x1F1FA }),
          cp({ 0x6FB3, 0x5927, 0x5229, 0x4E9A }),
        },
        { { "RU" },
          cp({ 0x1F1F7, 0x1F1FA }),
          cp({ 0x4FC4, 0x7F57, 0x65AF }),
        },
        { { "IN" },
          cp({ 0x1F1EE, 0x1F1F3 }),
          cp({ 0x5370, 0x5EA6 }),
        },
        { { "NL" },
          cp({ 0x1F1F3, 0x1F1F1 }),
          cp({ 0x8377, 0x5170 }),
        },
        { { "MY" },
          cp({ 0x1F1F2, 0x1F1FE }),
          cp({ 0x9A6C, 0x6765, 0x897F, 0x4E9A }),
        },
        { { "TH" },
          cp({ 0x1F1F9, 0x1F1ED }),
          cp({ 0x6CF0, 0x56FD }),
        },
        { { "PH" },
          cp({ 0x1F1F5, 0x1F1ED }),
          cp({ 0x83F2, 0x5F8B, 0x5BBE }),
        },
        { { "VN" },
          cp({ 0x1F1FB, 0x1F1F3 }),
          cp({ 0x8D8A, 0x5357 }),
        },
        { { "TR" },
          cp({ 0x1F1F9, 0x1F1F7 }),
          cp({ 0x571F, 0x8033, 0x5176 }),
        },
    };
    return kRows;
}

const CodePoints &fallbackFlag() // "其它"
{
    static const CodePoints value{ 0x1F310 };
    return value;
}

const CodePoints &fallbackName()
{
    static const CodePoints value{ 0x5176, 0x5B83 };
    return value;
}

}

namespace Gui {

CountryDisplay countryFlagAndName(const QString &code)
{
    const QString upper = code.toUpper(); // Rust 的 to_ascii_uppercase
    for (const Row &row : rows()) {
        for (const char *candidate : row.codes) {
            if (upper == QLatin1String(candidate)) {
                return {fromCodePoints(row.flag), fromCodePoints(row.name)};
            }
        }
    }
    return {fromCodePoints(fallbackFlag()), fromCodePoints(fallbackName())};
}

}
