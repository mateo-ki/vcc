#include "core/Encoding.h"

#include <QChar>
#include <QVector>

namespace {

const char *kStandardAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const char *kUrlSafeAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

// -2 表示 '='，-1 表示非法字符。
QVector<signed char> buildTable(const char *alphabet)
{
    QVector<signed char> table(256, signed(-1));
    for (int index = 0; index < 64; ++index) {
        table[uchar(alphabet[index])] = signed(index);
    }
    table[uchar('=')] = signed(-2);
    return table;
}

void appendGroup(int s0, int s1, int s2, int s3, int symbolCount, QByteArray &out)
{
    const int b0 = (s0 << 2) | (s1 >> 4);
    out.append(char(b0));
    if (symbolCount >= 3) {
        const int b1 = ((s1 & 0x0f) << 4) | (s2 >> 2);
        out.append(char(b1));
    } else if (symbolCount == 2) {
        // 残余比特必须为 0，否则不是规范的 base64（Rust 会报 InvalidLastSymbol）。
        return;
    }
    if (symbolCount >= 4) {
        const int b2 = ((s2 & 0x03) << 6) | s3;
        out.append(char(b2));
    }
}

bool trailingBitsClean(int s0, int s1, int s2, int s3, int symbolCount)
{
    if (symbolCount == 2) {
        return (s1 & 0x0f) == 0;
    }
    if (symbolCount == 3) {
        return (s2 & 0x03) == 0;
    }
    Q_UNUSED(s0);
    Q_UNUSED(s3);
    return true;
}

}

namespace Encoding {

QByteArray encode(const QByteArray &input, Engine engine)
{
    const char *alphabet = engine == Engine::Standard ? kStandardAlphabet : kUrlSafeAlphabet;
    QByteArray out;
    out.reserve(((input.size() + 2) / 3) * 4);

    int index = 0;
    while (index + 3 <= input.size()) {
        const uchar b0 = uchar(input.at(index));
        const uchar b1 = uchar(input.at(index + 1));
        const uchar b2 = uchar(input.at(index + 2));
        out.append(alphabet[b0 >> 2]);
        out.append(alphabet[((b0 & 0x03) << 4) | (b1 >> 4)]);
        out.append(alphabet[((b1 & 0x0f) << 2) | (b2 >> 6)]);
        out.append(alphabet[b2 & 0x3f]);
        index += 3;
    }

    const int remaining = input.size() - index;
    if (remaining == 1) {
        const uchar b0 = uchar(input.at(index));
        out.append(alphabet[b0 >> 2]);
        out.append(alphabet[(b0 & 0x03) << 4]);
        if (engine == Engine::Standard) {
            out.append("==");
        }
    } else if (remaining == 2) {
        const uchar b0 = uchar(input.at(index));
        const uchar b1 = uchar(input.at(index + 1));
        out.append(alphabet[b0 >> 2]);
        out.append(alphabet[((b0 & 0x03) << 4) | (b1 >> 4)]);
        out.append(alphabet[(b1 & 0x0f) << 2]);
        if (engine == Engine::Standard) {
            out.append('=');
        }
    }
    return out;
}

Result<QByteArray> decode(const QByteArray &input, Engine engine)
{
    const QVector<signed char> table =
        buildTable(engine == Engine::Standard ? kStandardAlphabet : kUrlSafeAlphabet);

    QByteArray symbols;
    symbols.reserve(input.size());
    int padding = 0;
    for (int index = 0; index < input.size(); ++index) {
        const signed char value = table[uchar(input.at(index))];
        if (value == -2) {
            // '=' 只能连续出现在末尾。
            for (int probe = index; probe < input.size(); ++probe) {
                if (table[uchar(input.at(probe))] != -2) {
                    return Result<QByteArray>::withError(
                        QStringLiteral("base64 填充出现在中部（位置 %1）").arg(probe));
                }
                padding += 1;
            }
            break;
        }
        if (value == -1) {
            return Result<QByteArray>::withError(
                QStringLiteral("base64 非法字符（位置 %1）").arg(index));
        }
        symbols.append(input.at(index));
    }

    if (engine == Engine::UrlSafeNoPad && padding > 0) {
        return Result<QByteArray>::withError(QStringLiteral("URL_SAFE_NO_PAD 不允许填充"));
    }
    if (engine == Engine::Standard && padding > 2) {
        return Result<QByteArray>::withError(QStringLiteral("base64 填充过多"));
    }
    if (symbols.size() % 4 == 1) {
        return Result<QByteArray>::withError(QStringLiteral("base64 长度非法"));
    }

    if (engine == Engine::Standard) {
        const int tail = symbols.size() % 4;
        const int expectedPadding = tail == 0 ? 0 : (4 - tail);
        if (padding != expectedPadding) {
            return Result<QByteArray>::withError(
                QStringLiteral("base64 填充不规范（期望 %1 个，实际 %2 个）")
                    .arg(expectedPadding)
                    .arg(padding));
        }
    } else if (padding != 0) {
        return Result<QByteArray>::withError(QStringLiteral("base64 不应有填充"));
    }

    QByteArray out;
    out.reserve((symbols.size() * 3) / 4 + 1);
    for (int offset = 0; offset < symbols.size(); offset += 4) {
        const int group = symbols.size() - offset;
        const int s0 = table[uchar(symbols.at(offset))];
        if (group == 1) {
            return Result<QByteArray>::withError(QStringLiteral("base64 尾部残缺"));
        }
        const int s1 = table[uchar(symbols.at(offset + 1))];
        const int s2 = group >= 3 ? table[uchar(symbols.at(offset + 2))] : 0;
        const int s3 = group >= 4 ? table[uchar(symbols.at(offset + 3))] : 0;
        if (!trailingBitsClean(s0, s1, s2, s3, group)) {
            return Result<QByteArray>::withError(QStringLiteral("base64 尾部残余比特非零"));
        }
        appendGroup(s0, s1, s2, s3, group, out);
    }
    return Result<QByteArray>::withValue(out);
}

Result<QByteArray> decodeLenient(const QByteArray &input)
{
    // 先把 padding 摘掉，按实际符号数补齐成规范形状，再复用严格校验（残余比特仍要干净）。
    QByteArray symbols;
    symbols.reserve(input.size());
    for (int index = 0; index < input.size(); ++index) {
        const char ch = input.at(index);
        if (ch == '=') {
            continue;
        }
        symbols.append(ch);
    }
    const int tail = symbols.size() % 4;
    if (tail == 1) {
        return Result<QByteArray>::withError(QStringLiteral("base64 长度非法"));
    }
    if (tail == 0) {
        return decode(symbols, Engine::Standard);
    }
    QByteArray padded = symbols;
    padded.append(4 - tail, '=');
    return decode(padded, Engine::Standard);
}

QString toHexLower(const QByteArray &bytes)
{
    static const char *digits = "0123456789abcdef";
    QString out;
    out.reserve(bytes.size() * 2);
    for (char ch : bytes) {
        const uchar value = uchar(ch);
        out.append(QLatin1Char(digits[value >> 4]));
        out.append(QLatin1Char(digits[value & 0x0f]));
    }
    return out;
}

Result<QByteArray> fromHex(const QString &hex)
{
    QString compact = hex.trimmed();
    if (compact.size() % 2 != 0) {
        return Result<QByteArray>::withError(QStringLiteral("十六进制长度不是偶数"));
    }
    QByteArray out;
    out.reserve(compact.size() / 2);
    for (int index = 0; index < compact.size(); index += 2) {
        bool ok = false;
        const int value = compact.mid(index, 2).toInt(&ok, 16);
        if (!ok) {
            return Result<QByteArray>::withError(
                QStringLiteral("十六进制非法字符: %1").arg(compact.mid(index, 2)));
        }
        out.append(char(value));
    }
    return Result<QByteArray>::withValue(out);
}

}
