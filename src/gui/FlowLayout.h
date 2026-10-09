#pragma once

#include <QLayout>
#include <QRect>
#include <QStyle>

#include <vector>

namespace Gui {

/// 会自动换行的横排。Rust 侧用 `horizontal_wrapped` 的理由写在注释里：动作条挂在行尾，
/// 960 的最小窗口宽度放不下 8 个地区药丸 + 4 个按钮，不换行就会被裁掉
/// ——「停止代理」被裁掉就等于没有。Qt 的标准布局里没有流式布局，所以自己实现一个。
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget *parent, int margin = -1, int hSpacing = -1, int vSpacing = -1)
        : QLayout(parent), hSpacing_(hSpacing), vSpacing_(vSpacing)
    {
        if (margin >= 0) {
            setContentsMargins(margin, margin, margin, margin);
        }
    }

    ~FlowLayout() override
    {
        clear();
    }

    void addItem(QLayoutItem *item) override;
    int horizontalSpacing() const;
    int verticalSpacing() const;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QSize minimumSize() const override;
    QSize sizeHint() const override;
    void setGeometry(const QRect &rect) override;
    QRect doLayout(const QRect &rect, bool testOnly) const;
    QLayoutItem *takeAt(int index) override;

private:
    void clear();
    int smartSpacing(QStyle::PixelMetric metric) const;

    std::vector<QLayoutItem *> items_;
    int hSpacing_ = -1;
    int vSpacing_ = -1;
};

}
