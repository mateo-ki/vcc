#pragma once

#include <QString>

// Rust 的 String Ord 按 UTF-8 字节序，等价于按码点比较；QString 默认按 UTF-16 码元，
// 代理对（emoji 国旗）会排到 BMP 字符之前。列表排序要和 Rust 一致就必须自己比。
namespace TextOrder {

inline int compare(const QString &left, const QString &right)
{
    const int leftSize = left.size();
    const int rightSize = right.size();
    int index = 0;
    while (index < leftSize && index < rightSize) {
        const QChar leftChar = left.at(index);
        const QChar rightChar = right.at(index);
        const bool leftHigh = QChar::isHighSurrogate(leftChar.unicode());
        const bool rightHigh = QChar::isHighSurrogate(rightChar.unicode());
        uint leftPoint = leftChar.unicode();
        uint rightPoint = rightChar.unicode();
        if (leftHigh && index + 1 < leftSize) {
            leftPoint = QChar::surrogateToUcs4(leftChar, left.at(index + 1));
        }
        if (rightHigh && index + 1 < rightSize) {
            rightPoint = QChar::surrogateToUcs4(rightChar, right.at(index + 1));
        }
        if (leftPoint != rightPoint) {
            return leftPoint < rightPoint ? -1 : 1;
        }
        index += (leftHigh || rightHigh) ? 2 : 1;
    }
    if (leftSize == rightSize) {
        return 0;
    }
    return leftSize < rightSize ? -1 : 1;
}

inline bool less(const QString &left, const QString &right)
{
    return compare(left, right) < 0;
}

/// 只折 ASCII 大写，与 Rust 的 to_ascii_lowercase 一致（中文/emoji 不受影响）。
inline QString asciiLower(const QString &text)
{
    QString out = text;
    for (QChar &character : out) {
        if (character.unicode() >= 'A' && character.unicode() <= 'Z') {
            character = QChar(character.unicode() + 32);
        }
    }
    return out;
}

}
