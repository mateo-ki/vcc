#include "core/Ids.h"

#include <QUuid>

namespace Ids {

QString newUuidV4()
{
    // Qt 6.10 没有 createUuidV4；createUuid() 自 Qt 5.11 起就是用 QRandomGenerator
    // 生成的 v4，输出已是小写，去掉花括号后与 Uuid::new_v4().to_string() 同形。
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

QString newUuidV4Simple()
{
    return newUuidV4().remove(QLatin1Char('-'));
}

}
