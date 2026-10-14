#pragma once

#include <QElapsedTimer>
#include <qtypes.h>

namespace Gui {

/// 往 GUI 投进度的闸门：高频回调（每颗节点、每个源）不该一条条变成界面事件。
/// 只被**驱动线程**碰，所以不需要锁；单独放头文件是为了能被直接单测钉住规则。
///
/// 规则只有两条：80ms 内的重复更新合并掉；**最后一次永远投出去**（否则进度会停在
/// "还差几颗"上，那是把丢一次事件放大成显示错误）。
class ProgressThrottle
{
public:
    /// 到点了该投一次吗？done 从 1 数起，total 是总数。
    bool shouldPost(size_t done, size_t total)
    {
        if (!clock_.isValid()) {
            clock_.start();
            lastPostMs_ = 0;
            return true;
        }
        const qint64 now = clock_.elapsed();
        const bool isLast = done + 1 >= total;
        if (!isLast && now - lastPostMs_ < kMinIntervalMs) {
            return false;
        }
        lastPostMs_ = now;
        return true;
    }

    static constexpr qint64 kMinIntervalMs = 80;

private:
    QElapsedTimer clock_;
    qint64 lastPostMs_ = 0;
};

} // namespace Gui
