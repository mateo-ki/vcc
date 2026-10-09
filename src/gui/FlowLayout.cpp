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

    for (QLayoutItem *item : items_) {
        const int nextX = x + item->sizeHint().width() + hSpace;
        if (lineHeight > 0 && nextX - hSpace > effective.right() + 1 && lineHeight > 0) {
            x = effective.x();
            y += lineHeight + vSpace;
            lineHeight = item->sizeHint().height();
        } else {
            // 第一个控件也要把行高立起来，否则整行高度按 0 算。
            lineHeight = std::max(lineHeight, item->sizeHint().height());
        }
        if (!testOnly) {
            item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));
        }
        x = nextX;
    }
    return QRect(rect.left(), rect.top(), rect.width(), y + lineHeight - rect.top());
}


}
