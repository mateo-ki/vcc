#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <utility>

namespace Updater {

/// 用 Rust `format!` 的占位符写法填充模板。中文模板由 tools/gen-kernel-cjk-codepoints.mjs
/// 从 Rust 源原样抄过来（src/kernel/KernelMessages.h），所以模板里保留着 `{name}` / `{}` /
/// `{:?}` 这些占位符。先替换具名占位符，再把剩下的 `{}` / `{:?}` 按位置依次填掉。
/// 和 SysProxy::fillTemplate / Platform::fillTemplate 同一套语义，这里自带一份，
/// 免得下载/安装这条链去依赖别的层。
inline QString fillTemplate(const QString &text,
                            const QVector<std::pair<QString, QString>> &named,
                            const QStringList &positional = {})
{
    QString out = text;
    for (const auto &entry : named) {
        out.replace(entry.first, entry.second);
    }
    if (!positional.isEmpty()) {
        int index = 0;
        int searchFrom = 0;
        while (index < positional.size()) {
            const int brace = out.indexOf(QStringLiteral("{}"), searchFrom);
            const int debug = out.indexOf(QStringLiteral("{:?}"), searchFrom);
            int at = -1;
            int length = 2;
            if (brace >= 0 && (debug < 0 || brace < debug)) {
                at = brace;
            } else if (debug >= 0) {
                at = debug;
                length = 4;
            }
            if (at < 0) {
                break;
            }
            out.replace(at, length, positional.at(index));
            searchFrom = at + positional.at(index).length();
            index += 1;
        }
    }
    return out;
}

}
