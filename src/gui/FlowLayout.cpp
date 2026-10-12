#include "gui/FlowLayout.h"

#include <QWidget>

namespace Gui {

void FlowLayout::addItem(QLayoutItem *item)
{
    items_.push_back(item);
}

int FlowLayout::horizontalSpacing() const
{
    if (hSpacing_ >= 0) {
        return hSpacing_;
    }
    return smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
}

int FlowLayout::verticalSpacing() const
{
    if (vSpacing_ >= 0) {
        return vSpacing_;
    }
    return smartSpacing(QStyle::PM_LayoutVerticalSpacing);
}

int FlowLayout::count() const
{
    return static_cast<int>(items_.size());
}

QLayoutItem *FlowLayout::itemAt(int index) const
{
    return index >= 0 && index < count() ? items_[static_cast<size_t>(index)] : nullptr;
}

QLayoutItem *FlowLayout::takeAt(int index)
{
    if (index < 0 || index >= count()) {
        return nullptr;
    }
    auto it = items_.begin() + index;
    QLayoutItem *item = *it;
    items_.erase(it);
    return item;
}

void FlowLayout::clear()
{
    for (QLayoutItem *item : items_) {
        delete item;
    }
    items_.clear();
}

int FlowLayout::smartSpacing(QStyle::PixelMetric metric) const
{
    const QWidget *parentWidget = this->parentWidget();
    if (parentWidget == nullptr) {
        return 6;
    }
    return parentWidget->style()->pixelMetric(metric, nullptr, parentWidget);
}

Qt::Orientations FlowLayout::expandingDirections() const
{
    return {};
}

bool FlowLayout::hasHeightForWidth() const
{
    return true;
}

int FlowLayout::heightForWidth(int width) const
{
    const QRect placed = doLayout({0, 0, width, 0}, true);
    return placed.height();
}

void FlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (QLayoutItem *item : items_) {
        size = size.expandedTo(item->minimumSize());
    }
    return size;
}

QSize FlowLayout::sizeHint() const
{
    return minimumSize();
}

QRect FlowLayout::doLayout(const QRect &rect, bool testOnly) const
{
    const QRect effective = rect.adjusted(0, 0, 0, 0);
    int x = effective.x();
    int y = effective.y();
    int lineHeight = 0;
    const int hSpace = horizontalSpacing();
    const int vSpace = verticalSpacing();

    struct LineEntry {
        QLayoutItem *item;
        int x;
        int w;
        int h;
    };
    QVector<LineEntry> lineItems;

    auto flushLine = [&](int lineY, int lineH) {
        if (!testOnly) {
            for (const auto &entry : lineItems) {
                // 垂直居中对齐：使行内的文字标签与按钮药丸沿中轴对齐
                const int itemY = lineY + (lineH - entry.h) / 2;
                entry.item->setGeometry(QRect(entry.x, itemY, entry.w, entry.h));
            }
        }
        lineItems.clear();
    };

    for (QLayoutItem *item : items_) {
        const QSize hint = item->sizeHint();
        const int itemW = hint.width();
        const int itemH = hint.height();
        const int nextX = x + itemW + hSpace;

        if (lineHeight > 0 && nextX - hSpace > effective.right() + 1) {
            flushLine(y, lineHeight);
            x = effective.x();
            y += lineHeight + vSpace;
            lineHeight = itemH;
        } else {
            lineHeight = std::max(lineHeight, itemH);
        }

        lineItems.push_back({item, x, itemW, itemH});
        x += itemW + hSpace;
    }

    if (!lineItems.isEmpty()) {
        flushLine(y, lineHeight);
    }

    return QRect(rect.left(), rect.top(), rect.width(), y + lineHeight - rect.top());
}


}
