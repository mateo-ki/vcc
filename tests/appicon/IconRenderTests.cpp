// 一次性取证工具：把一张 SVG 渲成 128/48/32/16 的对照表（浅底 + 深底各一行），
// 用来在**没有浏览器可截图**的环境里自己看图标画成了什么样。
// 跑法：VCC_ICON_SVG=<svg 路径> VCC_ICON_PNG=<输出 png> 跑 vccIconRender
#include <gtest/gtest.h>

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#include <iostream>

namespace {

TEST(IconRender, contactSheet)
{
    const QString svg = qEnvironmentVariable("VCC_ICON_SVG");
    const QString out = qEnvironmentVariable("VCC_ICON_PNG");
    if (svg.isEmpty() || out.isEmpty()) {
        GTEST_SKIP() << "需要 VCC_ICON_SVG 和 VCC_ICON_PNG";
    }
    QSvgRenderer renderer(svg);
    ASSERT_TRUE(renderer.isValid()) << svg.toStdString();

    const int sizes[] = {128, 48, 32, 16};
    QImage canvas(340, 320, QImage::Format_ARGB32);
    canvas.fill(QColor("#eef1f5"));
    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setPen(QColor("#1c2430"));
    p.drawText(12, 20, QStringLiteral("浅底"));
    p.drawText(12, 176, QStringLiteral("深底"));
    int x = 12;
    for (const int size : sizes) {
        // 浅底：白块打底，再把 SVG 画上去
        p.fillRect(QRect(x - 4, 26, size + 8, size + 8), QColor("#ffffff"));
        renderer.render(&p, QRectF(x, 30, size, size));
        // 深底：先在临时图上铺深色，再把 SVG 画进那张图
        QImage darkImage(size, size, QImage::Format_ARGB32);
        darkImage.fill(QColor("#12181f"));
        QPixmap dark = QPixmap::fromImage(darkImage);
        QPainter dp(&dark);
        renderer.render(&dp, QRectF(0, 0, size, size));
        dp.end();
        p.drawPixmap(x, 186, dark);
        p.setPen(QColor("#6b7480"));
        p.drawText(x, 186 + size + 14, QStringLiteral("%1").arg(size));
        p.setPen(QColor("#1c2430"));
        x += size + 26;
    }
    p.end();
    ASSERT_TRUE(canvas.save(out)) << out.toStdString();
    std::cout << "ICON wrote " << out.toStdString() << std::endl;
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
