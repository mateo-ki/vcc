#pragma once

#include <nlohmann/json.hpp>

#include <optional>

#include <QString>

#include "core/Result.h"

// serde_json 的默认 map 是 BTreeMap（按键字节序输出），nlohmann::json 的默认
// std::map 行为一致；两者输出都是紧凑无空格。库里 nodes.config 列就是这个形状。
using Json = nlohmann::json;

namespace JsonUtil {

/// 紧凑序列化，等价于 serde_json::to_string。
inline QString dump(const Json &value)
{
    return QString::fromStdString(value.dump());
}

/// 从 UTF-8 文本解析，失败返回错误文本（Rust 侧多数调用点会 unwrap 成 `{}`）。
Result<Json> parse(const QString &text);

inline Json parseOrEmptyObject(const QString &text)
{
    const Result<Json> result = parse(text);
    return result.isOk() ? result.value() : Json::object();
}

// ---- 可空约定：QString 的 null 态 ↔ Rust 的 Option<String> ----

/// null QString → JSON null，其余按 UTF-8 字符串写出。
inline Json optText(const QString &text)
{
    return text.isNull() ? Json() : Json(text.toStdString());
}

inline QString textOrEmpty(const Json &value, const char *key)
{
    if (!value.is_object()) {
        return QString();
    }
    const auto found = value.find(key);
    if (found == value.end() || found->is_null()) {
        return QString();
    }
    if (found->is_string()) {
        return QString::fromStdString(found->get<std::string>());
    }
    if (found->is_number_integer() || found->is_number_unsigned()) {
        return QString::number(found->get<qint64>());
    }
    return QString::fromStdString(found->dump());
}

/// 区分"缺键/null"和"空串"：缺键或 null 返回 null QString。
inline QString nullableText(const Json &value, const char *key)
{
    if (!value.is_object()) {
        return QString();
    }
    const auto found = value.find(key);
    if (found == value.end() || found->is_null()) {
        return QString();
    }
    if (!found->is_string()) {
        return QString();
    }
    return QString::fromStdString(found->get<std::string>());
}

inline bool boolOr(const Json &value, const char *key, bool fallback)
{
    if (!value.is_object()) {
        return fallback;
    }
    const auto found = value.find(key);
    if (found == value.end() || found->is_null()) {
        return fallback;
    }
    if (found->is_boolean()) {
        return found->get<bool>();
    }
    if (found->is_number()) {
        return found->get<double>() != 0.0;
    }
    return fallback;
}

inline qint64 intOr(const Json &value, const char *key, qint64 fallback)
{
    if (!value.is_object()) {
        return fallback;
    }
    const auto found = value.find(key);
    if (found == value.end() || found->is_null()) {
        return fallback;
    }
    if (found->is_number()) {
        return found->get<qint64>();
    }
    if (found->is_string()) {
        bool ok = false;
        const qint64 parsed = QString::fromStdString(found->get<std::string>()).toLongLong(&ok);
        return ok ? parsed : fallback;
    }
    return fallback;
}

inline quint64 uintOr(const Json &value, const char *key, quint64 fallback)
{
    if (!value.is_object()) {
        return fallback;
    }
    const auto found = value.find(key);
    if (found == value.end() || found->is_null()) {
        return fallback;
    }
    if (found->is_number()) {
        return found->get<quint64>();
    }
    if (found->is_string()) {
        bool ok = false;
        const quint64 parsed = QString::fromStdString(found->get<std::string>()).toULongLong(&ok);
        return ok ? parsed : fallback;
    }
    return fallback;
}

inline double doubleOr(const Json &value, const char *key, double fallback)
{
    if (!value.is_object()) {
        return fallback;
    }
    const auto found = value.find(key);
    if (found == value.end() || found->is_null() || !found->is_number()) {
        return fallback;
    }
    return found->get<double>();
}

// ---- 部分更新用：std::optional 直接对应 Rust 的 Option<T> ----

/// 键缺失或为 null → nullopt；否则按 bool 读（数字/字符串也容错，与 serde 宽松输入一致）。
inline std::optional<bool> optBool(const Json &value, const char *key)
{
    if (!value.is_object()) {
        return std::nullopt;
    }
    const auto found = value.find(key);
    if (found == value.end() || found->is_null()) {
        return std::nullopt;
    }
    if (found->is_boolean()) {
        return found->get<bool>();
    }
    if (found->is_number()) {
        return found->get<double>() != 0.0;
    }
    return std::nullopt;
}

inline std::optional<qint64> optInt(const Json &value, const char *key)
{
    if (!value.is_object()) {
        return std::nullopt;
    }
    const auto found = value.find(key);
    if (found == value.end() || found->is_null() || !found->is_number()) {
        return std::nullopt;
    }
    return found->get<qint64>();
}

inline std::optional<double> optDouble(const Json &value, const char *key)
{
    if (!value.is_object()) {
        return std::nullopt;
    }
    const auto found = value.find(key);
    if (found == value.end() || found->is_null() || !found->is_number()) {
        return std::nullopt;
    }
    return found->get<double>();
}

/// 键存在且为字符串才返回（空串也算存在），对应 serde 里 Option<String> 的"给了就改"。
inline std::optional<QString> optTextValue(const Json &value, const char *key)
{
    if (!value.is_object()) {
        return std::nullopt;
    }
    const auto found = value.find(key);
    if (found == value.end() || !found->is_string()) {
        return std::nullopt;
    }
    return QString::fromStdString(found->get<std::string>());
}

}
