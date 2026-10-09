#include "gui/Widgets.h"

namespace Gui {

QString formatBytes(quint64 bytes)
{
    const double value = static_cast<double>(bytes);
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        return QString::number(value / (1024.0 * 1024.0 * 1024.0), 'f', 2)
            + QStringLiteral(" GB");
    }
    if (bytes >= 1024ULL * 1024ULL) {
        return QString::number(value / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB");
    }
    if (bytes >= 1024ULL) {
        return QString::number(value / 1024.0, 'f', 1) + QStringLiteral(" KB");
    }
    return QString::number(value) + QStringLiteral(" B");
}

}
