#include "models/Source.h"

#include "core/Ids.h"

namespace Models {

using JsonUtil::boolOr;
using JsonUtil::dump;
using JsonUtil::intOr;
using JsonUtil::nullableText;
using JsonUtil::optText;
using JsonUtil::textOrEmpty;

Source Source::scratch(const QString &id, const QString &name, const QString &urlTemplate)
{
    Source source;
    source.id = id;
    source.name = name;
    source.urlTemplate = urlTemplate;
    source.enabled = true;
    source.createdAt = TimeUtil::OptionalUtc::now();
    source.updatedAt = TimeUtil::OptionalUtc::now();
    return source;
}

Json Source::toJson() const
{
    Json value;
    value["id"] = id.toStdString();
    value["name"] = name.toStdString();
    value["url_template"] = urlTemplate.toStdString();
    value["enabled"] = enabled;
    value["remark"] = optText(remark);
    value["last_fetch_at"] = optText(lastFetchAt.toJsonText());
    value["last_status"] = optText(lastStatus);
    value["last_node_count"] = lastNodeCount;
    value["last_resolved_url"] = optText(lastResolvedUrl);
    value["last_etag"] = optText(lastEtag);
    value["last_modified"] = optText(lastModified);
    value["content_sha256"] = optText(contentSha256);
    value["fail_streak"] = failStreak;
    value["last_fetch_host"] = optText(lastFetchHost);
    value["push_disabled"] = pushDisabled;
    value["created_at"] = optText(createdAt.toJsonText());
    value["updated_at"] = optText(updatedAt.toJsonText());
    return value;
}

Source Source::fromJson(const Json &value)
{
    Source source;
    source.id = textOrEmpty(value, "id");
    source.name = textOrEmpty(value, "name");
    source.urlTemplate = textOrEmpty(value, "url_template");
    source.enabled = boolOr(value, "enabled", true);
    source.remark = nullableText(value, "remark");
    source.lastFetchAt = TimeUtil::OptionalUtc::fromText(nullableText(value, "last_fetch_at"));
    source.lastStatus = nullableText(value, "last_status");
    source.lastNodeCount = intOr(value, "last_node_count", 0);
    source.lastResolvedUrl = nullableText(value, "last_resolved_url");
    source.lastEtag = nullableText(value, "last_etag");
    source.lastModified = nullableText(value, "last_modified");
    source.contentSha256 = nullableText(value, "content_sha256");
    source.failStreak = intOr(value, "fail_streak", 0);
    source.lastFetchHost = nullableText(value, "last_fetch_host");
    source.pushDisabled = boolOr(value, "push_disabled", false);
    source.createdAt = TimeUtil::OptionalUtc::fromText(nullableText(value, "created_at"));
    source.updatedAt = TimeUtil::OptionalUtc::fromText(nullableText(value, "updated_at"));
    return source;
}

CreateSource CreateSource::fromJson(const Json &value)
{
    CreateSource created;
    created.name = textOrEmpty(value, "name");
    created.urlTemplate = textOrEmpty(value, "url_template");
    created.enabled = boolOr(value, "enabled", true);
    created.remark = nullableText(value, "remark");
    return created;
}

UpdateSource UpdateSource::fromJson(const Json &value)
{
    UpdateSource update;
    update.name = JsonUtil::optTextValue(value, "name");
    update.urlTemplate = JsonUtil::optTextValue(value, "url_template");
    update.enabled = JsonUtil::optBool(value, "enabled");
    update.remark = JsonUtil::optTextValue(value, "remark");
    update.pushDisabled = JsonUtil::optBool(value, "push_disabled");
    return update;
}

}
