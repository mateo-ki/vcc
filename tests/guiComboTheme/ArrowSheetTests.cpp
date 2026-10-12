// 一次性取证工具：把下拉框/旋钮的箭头三种画法渲成 PNG，肉眼挑。
// 跑法：VCC_ARROW_SHEET=<输出png> 跑 vccComboThemeTests（或任一 GUI 套件）。
#include <gtest/gtest.h>

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QPolygonF>
#include <QPixmap>
#include <QSpinBox>
#include <QStandardPaths>
#include <QString>

#include "gui/Theme.h"
#include "gui/Widgets.h"

namespace {

QString arrowPng(const QString &name, const QColor &color, bool up)
{
    const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("vcc-arrow-probe"));
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QStringLiteral("%1-%2.png")
                                               .arg(name)
                                               .arg(color.name(QColor::HexArgb)));
    if (QFile::exists(path)) {
        return path;
    }
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 2.0);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    if (up) {
        p.drawPolyline(QPolygonF() << QPointF(5, 13) << QPointF(10, 7) << QPointF(15, 13));
    } else {
        p.drawPolyline(QPolygonF() << QPointF(5, 7) << QPointF(10, 13) << QPointF(15, 7));
    }
    p.end();
    if (!pixmap.save(path)) {
        return QString();
    }
    return path;
}

void fillCombo(QComboBox *combo)
{
    combo->addItems({QStringLiteral("跟随"), QStringLiteral("Sing-box"), QStringLiteral("Xray")});
    combo->setCurrentIndex(1);
}

TEST(ArrowSheet, rendersThreeArrowStrategies)
{
    const QString out = qEnvironmentVariable("VCC_ARROW_SHEET");
    if (out.isEmpty()) {
        GTEST_SKIP() << "no VCC_ARROW_SHEET";
    }
    const Gui::Theme::Palette pal = Gui::Theme::current();
    QString png = arrowPng(QStringLiteral("down"), pal.textSecondary, false);
    QString pngUp = arrowPng(QStringLiteral("up"), pal.textSecondary, true);
    png.replace(QLatin1Char('\\'), QLatin1Char('/'));
    pngUp.replace(QLatin1Char('\\'), QLatin1Char('/'));
    QString qssC = QStringLiteral(
                      "QComboBox { background-color: %1; color: %2; border: 1px solid %3;"
                      " border-radius: 8px; padding: 4px 8px; }"
                      "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position:"
                      " top right; width: 22px; border-left: 1px solid %3; background-color: %4; }"
                      "QComboBox::down-arrow { image: url(%5); width: 10px; height: 10px; }")
                      .arg(pal.panel.name(), pal.textPrimary.name(),
                           pal.border.name(), pal.panelHover.name(),
                           png.replace('\\', '/'));
    QString qssSpin = QStringLiteral(
                          "QSpinBox { background-color: %1; color: %2; border: 1px solid %3;"
                          " border-radius: 8px; }"
                          "QSpinBox::up-button { subcontrol-origin: border; subcontrol-position:"
                          " top right; width: 16px; border-left: 1px solid %3; }"
                          "QSpinBox::down-button { subcontrol-origin: border; subcontrol-position:"
                          " bottom right; width: 16px; border-left: 1px solid %3; }"
                          "QSpinBox::up-arrow { image: url(%4); width: 10px; height: 10px; }"
                          "QSpinBox::down-arrow { image: url(%5); width: 10px; height: 10px; }")
                          .arg(pal.panel.name(), pal.textPrimary.name(),
                               pal.border.name(), pngUp.replace('\\', '/'),
                               png.replace('\\', '/'));

    QImage canvas(520, 260, QImage::Format_ARGB32);
    canvas.fill(pal.pageBg);
    QPainter p(&canvas);
    p.setPen(pal.textPrimary);

    auto *a = new QComboBox;
    fillCombo(a);
    Gui::Theme::setWidgetCss(a, QStringLiteral(
                                    "background-color: {panel}; color: {textPrimary};"
                                    " border: 1px solid {border}; border-radius: 8px;"));
    Gui::decorateComboBox(a);   // 现状
    a->setGeometry(20, 40, 220, 32);
    a->render(&p, QPoint(20, 40));
    p.drawText(20, 30, QStringLiteral("A = 现状（无箭头）"));

    auto *b = new QComboBox;
    fillCombo(b);
    b->setGeometry(20, 100, 220, 32);
    b->render(&p, QPoint(20, 100));
    p.drawText(20, 90, QStringLiteral("B = 完全不加样式（原生箭头）"));

    auto *c = new QComboBox;
    fillCombo(c);
    c->setStyleSheet(qssC);
    c->setGeometry(20, 160, 220, 32);
    c->render(&p, QPoint(20, 160));
    p.drawText(20, 150, QStringLiteral("C = 自带 PNG 箭头"));

    auto *spin = new QSpinBox;
    spin->setRange(1, 65535);
    spin->setValue(9980);
    spin->setStyleSheet(qssSpin);
    spin->setGeometry(300, 160, 160, 32);
    spin->render(&p, QPoint(300, 160));
    p.drawText(300, 150, QStringLiteral("C2 = 旋钮 PNG 箭头"));

    auto *spinNative = new QSpinBox;
    spinNative->setRange(1, 65535);
    spinNative->setValue(9980);
    spinNative->setGeometry(300, 40, 160, 32);
    spinNative->render(&p, QPoint(300, 40));
    p.drawText(300, 30, QStringLiteral("B2 = 旋钮不加样式"));

    auto *spinStyled = new QSpinBox;
    spinStyled->setRange(1, 65535);
    spinStyled->setValue(9980);
    Gui::decorateFieldsIn(spinStyled->parentWidget());   // 现状那套 border 三角
    spinStyled->setGeometry(300, 100, 160, 32);
    spinStyled->render(&p, QPoint(300, 100));
    p.drawText(300, 90, QStringLiteral("A2 = 现状 border 三角"));
    p.end();

    ASSERT_TRUE(canvas.save(out)) << "写不出 " << out.toStdString();
    delete a;
    delete b;
    delete c;
    delete spin;
    delete spinNative;
    delete spinStyled;
}

} // namespace
