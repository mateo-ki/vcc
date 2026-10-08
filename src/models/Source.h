#pragma once

#include <optional>

#include <QString>

#include "core/JsonUtil.h"
#include "core/TimeUtil.h"

// serde 默认按字段名原样输出（snake_case），Option 为 None 时输出 null。
//
// 可空文本约定：QString 的 null 态表示 Rust 的 Option<String>。QString() 是 null
// （→ SQL NULL / JSON null），QString("") 是非空空串（→ '' / ""）。
namespace Models {

/// 可周期性拉取的订阅源。URL 模板支持 {Ymd} {Y} {m} {mm} {d} {dd} {ymd} 占位符。
struct Source {
    QString id;
    QString name;
    QString urlTemplate;
    bool enabled = true;
    QString remark;
    TimeUtil::OptionalUtc lastFetchAt;
    QString lastStatus;
    qint64 lastNodeCount = 0;
    QString lastResolvedUrl;
    QString lastEtag;
    QString lastModified;
    QString contentSha256;
    qint64 failStreak = 0;
    QString lastFetchHost;
    bool pushDisabled = false;
    TimeUtil::OptionalUtc createdAt;
    TimeUtil::OptionalUtc updatedAt;

    /// 无库记录的临时源对象：只用于复用下载/解析路径（gist 发现等），不会写库。
    static Source scratch(const QString &id, const QString &name, const QString &urlTemplate);

    Json toJson() const;
    static Source fromJson(const Json &value);
};

struct CreateSource {
    QString name;
    QString urlTemplate;
    bool enabled = true;
    QString remark;

    static CreateSource fromJson(const Json &value);
};

/// 部分更新：JSON 里出现过的键才改，未出现的保持原值。
struct UpdateSource {
    std::optional<QString> name;
    std::optional<QString> urlTemplate;
    std::optional<bool> enabled;
    std::optional<QString> remark;
    std::optional<bool> pushDisabled;

    static UpdateSource fromJson(const Json &value);
};

}
