#include "core/JsonUtil.h"

namespace JsonUtil {

Result<Json> parse(const QString &text)
{
    if (text.isNull() || text.trimmed().isEmpty()) {
        return Result<Json>::withError(QStringLiteral("空 JSON"));
    }
    try {
        return Result<Json>::withValue(Json::parse(text.toStdString()));
    } catch (const Json::exception &error) {
        return Result<Json>::withError(
            QStringLiteral("JSON 解析失败: %1").arg(QString::fromLatin1(error.what())));
    }
}

}
