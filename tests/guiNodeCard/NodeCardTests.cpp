// 控制台「可用节点」栅格的两项验收：
//  1) 自绘卡片要和"每卡一份 CSS"的旧做法逐像素一致，卡片子树里不许留样式表；
//     只剩一个节点时不许被行高拉满。
//  2) 栅格按页画（一页 50 张卡）：卡片数、翻页可用位、筛选回到第一页。
// 期望文案全部取 tools/gen-cjk-codepoints.mjs 从 Rust 源码生成的 UiText，测试里不手打中文。
#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QFrame>
#include <QLabel>
#include <QMap>
#include <QPalette>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <iostream>

#include "app/AppContext.h"
#include "core/TimeUtil.h"
#include "db/Database.h"
#include "gui/NodeCard.h"
#include "gui/ProxyPage.h"
#include "gui/Theme.h"
#include "gui/UiText.h"
#include "models/Node.h"
#include "models/Source.h"
#include "proxy/ProxyManager.h"

using namespace Gui;

namespace {

constexpr int kCardWidth = 260;
constexpr int kCardHeight = 96;
/// `renderOnMagenta` 里卡片在 stage 内的摆放偏移：采样坐标必须加上它，见
/// paintedCardMatchesTheThemeStyleSheetCard。
constexpr int kStagePad = 10;

std::shared_ptr<App::AppContext> makeContext(const QString &directory)
{
    App::AppContext::Options options;
    options.dbPath = QDir(directory).filePath(QStringLiteral("vpn-parser.db"));
    const auto created = App::AppContext::create(options);
    if (created.isErr()) {
        ADD_FAILURE() << created.error().toStdString();
        return nullptr;
    }
    return created.value();
}

Models::Node seedNode(int index)
{
    Models::Node node;
    node.id = QStringLiteral("n-%1").arg(index);
    node.protocol = QStringLiteral("ss");
    node.name = QStringLiteral("NODE-%1").arg(index);
    node.server = QStringLiteral("10.0.0.%1").arg(index + 1);
    node.port = 443;
    node.country = QStringLiteral("HK");
    node.raw = QStringLiteral("ss://a@10.0.0.%1:443").arg(index + 1);
    node.config = Json::object();
    node.latencyOk = std::optional<bool>(true);
    // 延迟按序号递增：默认排序是"按延迟"，这样第几页的第一张卡是可以预测的。
    node.latencyMs = qint64(1000 + index);
    node.latencyTestKind = QStringLiteral("google_proxy");
    node.createdAt = TimeUtil::OptionalUtc::now();
    node.updatedAt = TimeUtil::OptionalUtc::now();
    return node;
}

/// 灌一个源 + count 个"已测通"节点：可用节点查询要求 latency_ok=1 且
/// latency_test_kind='google_proxy'，缺任何一个这一页就一条都取不到。
void seedNodes(App::AppContext &context, int count)
{
    Models::CreateSource input;
    input.name = QStringLiteral("seed");
    input.urlTemplate = QStringLiteral("https://example.com/sub");
    const auto created = context.database().createSource(input);
    ASSERT_TRUE(created.isOk()) << created.error().toStdString();
    const QString sourceId = created.value().id;

    QVector<Models::Node> nodes;
    for (int i = 0; i < count; ++i) {
        Models::Node node = seedNode(i);
        node.sourceId = sourceId;
        nodes.push_back(node);
    }
    const auto written = context.database().replaceSourceNodes(sourceId, nodes);
    ASSERT_TRUE(written.isOk()) << written.error().toStdString();
}

int channelDelta(QColor a, QColor b)
{
    return std::max({std::abs(a.red() - b.red()), std::abs(a.green() - b.green()),
                     std::abs(a.blue() - b.blue())});
}

/// 非品红像素的包围盒 = 真正画出来的那颗徽章/卡片。
QSize inkBox(const QImage &image)
{
    const QColor magenta(255, 0, 255);
    QRect box;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (channelDelta(QColor(image.pixel(x, y)), magenta) > 24) {
                box = box.isNull() ? QRect(x, y, 1, 1) : box.united(QRect(x, y, 1, 1));
            }
        }
    }
    return box.size();
}

/// 徽章真正画出来的两种主色：底色（最多的非品红色）和文字色（第二多的）。
/// 不能按固定坐标取色：自绘和 QSS 的字居中差 1px，取色点就会从"两笔之间"
/// 落到"笔画上"，比出的是字体排布而不是配色。
QVector<QColor> dominantColors(const QImage &image)
{
    const QColor magenta(255, 0, 255);
    QMap<QRgb, int> counts;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            if (channelDelta(QColor(pixel), magenta) > 24) {
                ++counts[pixel];
            }
        }
    }
    QVector<QPair<int, QColor>> ranked;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        ranked.push_back({it.value(), QColor(it.key())});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto &left, const auto &right) {
        return left.first > right.first;
    });
    QVector<QColor> out;
    for (int index = 0; index < std::min(2, static_cast<int>(ranked.size())); ++index) {
        out.push_back(ranked.at(index).second);
    }
    return out;
}

/// 放到一块纯色底上真显示一次再抓。底色必须走 QPalette：选择器为空的 QSS
/// 会连子控件一起铺底，那样量的就不是这张卡了。
QImage renderOnMagenta(QWidget *widget)
{
    QWidget *stage = new QWidget;
    stage->setFixedSize(widget->width() + 2 * kStagePad, widget->height() + 2 * kStagePad);
    stage->setAutoFillBackground(true);
    QPalette palette = stage->palette();
    palette.setColor(QPalette::Window, QColor(255, 0, 255));
    stage->setPalette(palette);

    widget->setParent(stage);
    widget->move(kStagePad, kStagePad);
    widget->show();
    stage->show();
    QCoreApplication::processEvents();
    const QImage image = stage->grab().toImage();
    widget->setParent(nullptr);          // 归还所有权：调用方还要 delete 它
    delete stage;
    return image;
}

/// 改造前那张卡：CSS 就是当时 ProxyPage.cpp 里的原文（纯 ASCII，可以照抄）。
QFrame *legacyCard(const QString &css)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("RefCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setStyleSheet(css);
    card->resize(kCardWidth, kCardHeight);
    return card;
}

bool waitRows(ProxyPage &page, int wanted)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 6000 && page.rowCount() != wanted) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    QCoreApplication::processEvents();
    return page.rowCount() == wanted;
}

QPushButton *pagerButton(ProxyPage &page, const char *name)
{
    for (auto *button : page.findChildren<QPushButton *>()) {
        if (button->objectName() == QLatin1String(name)) {
            return button;
        }
    }
    return nullptr;
}

QStringList cardNames(ProxyPage &page)
{
    QStringList out;
    const auto cards = page.findChildren<QFrame *>(QStringLiteral("NodeCard"));
    for (auto *card : cards) {
        if (card->isVisibleTo(card->parentWidget())) {
            out.push_back(card->property("nodeName").toString());
        }
    }
    return out;
}

}

/// 参照 CSS 由当前调色板拼出来，不写死 hex：卡片现在画的是主题语义色，
/// 参照要是还留着当初那串 #e2e8f0/#2563eb，量的就是"和旧稿一致"而不是"和主题一致"。
QString referenceCardCss(const QString &fillSlot, int borderWidth, const QString &borderSlot)
{
    return QStringLiteral("QFrame#RefCard { background-color: %1; border: %2px solid %3;"
                          " border-radius: 12px; }")
        .arg(Gui::Theme::cssWithPalette(QStringLiteral("{%1}").arg(fillSlot)))
        .arg(borderWidth)
        .arg(Gui::Theme::cssWithPalette(QStringLiteral("{%1}").arg(borderSlot)));
}

TEST(NodeCardTests, paintedCardMatchesTheThemeStyleSheetCard)
{
    struct Case {
        const char *name;
        const char *fillSlot;
        const char *borderSlot;
        int borderWidth;
        NodeCardState state;
    };
    const QVector<Case> cases = {
        {"normal", "panel", "borderSubtle", 1, NodeCardState::Normal},
        {"selected", "accentBg", "accent", 2, NodeCardState::Selected},
        {"running", "successBg", "success", 2, NodeCardState::Running},
    };
    // 中心看填充，上边/左边看描边，左上角看圆角外是否仍然露着底色。
    const QVector<QPoint> samples = {
        {kCardWidth / 2, kCardHeight / 2}, {kCardWidth / 2, 1}, {kCardWidth / 2, 3},
        {1, kCardHeight / 2},              {0, 0},               {2, 2},
    };

    for (const auto &item : cases) {
        QFrame *legacy = legacyCard(referenceCardCss(item.fillSlot, item.borderWidth,
                                                     item.borderSlot));
        auto *painted = new NodeCardFrame;
        painted->resize(kCardWidth, kCardHeight);
        painted->setState(item.state);

        const QImage before = renderOnMagenta(legacy);
        const QImage after = renderOnMagenta(painted);
        for (const auto &point : samples) {
            // `renderOnMagenta` 把控件摆在 stage 的 (kStagePad, kStagePad)，抓的是整张
            // stage：所以卡片坐标必须加这个偏移，否则量的根本不是那个点（以前恰好落在
            // 白色区域上，才让这条测试一直是假绿）。
            const QPoint at(point.x() + kStagePad, point.y() + kStagePad);
            const QColor left(before.pixel(at.x(), at.y()));
            const QColor right(after.pixel(at.x(), at.y()));
            EXPECT_LE(channelDelta(left, right), 3)
                << item.name << " 在 (" << point.x() << "," << point.y()
                << ") 处颜色变了: " << left.name(QColor::HexRgb).toStdString() << " vs "
                << right.name(QColor::HexRgb).toStdString();
        }
        delete painted;
        delete legacy;
    }
}

TEST(NodeCardTests, badgesMatchTheirThemeStyleSheetBox)
{
    struct Case {
        const char *name;
        const char *backgroundSlot;
        const char *foregroundSlot;
        int pixelSize;
        int qssPadding;
        NodeBadge::Tone tone;
    };
    // 字号/内边距沿用改造前那三段 CSS 的原值（6px / 8px）：QSS 盒模型和
    // contentsMargins 的算法差一个常数，NodeBadge::setTone 里的 sidePadding 就是照这个
    // 校准的；这里要是再跟着换算，比的就不是同一颗徽章了。
    const QVector<Case> cases = {
        {"protocol", "panelHover", "textSecondary", 10, 6, NodeBadge::Tone::Protocol},
        {"runningBadge", "successBg", "success", 11, 8, NodeBadge::Tone::Running},
        {"selectedBadge", "accentBg", "accentStrong", 11, 8, NodeBadge::Tone::Selected},
    };

    for (const auto &item : cases) {
        const QString css = QStringLiteral("QLabel#Ref { background: %1; color: %2;"
                                           " font-weight: 700; font-size: %3px;"
                                           " border-radius: 4px; padding: 2px %4px; }")
                                .arg(Gui::Theme::cssWithPalette(
                                         QStringLiteral("{%1}").arg(item.backgroundSlot)),
                                     Gui::Theme::cssWithPalette(
                                         QStringLiteral("{%1}").arg(item.foregroundSlot)))
                                .arg(item.pixelSize)
                                .arg(item.qssPadding);
        auto *legacy = new QLabel(QStringLiteral("SS"));
        legacy->setObjectName(QStringLiteral("Ref"));
        legacy->setStyleSheet(css);
        legacy->show();
        QCoreApplication::processEvents();     // 先 polish，QSS 的 padding 才进 sizeHint
        legacy->resize(legacy->sizeHint());

        auto *badge = new NodeBadge(QStringLiteral("SS"));
        badge->setTone(item.tone);
        badge->show();
        QCoreApplication::processEvents();
        badge->resize(badge->sizeHint());

        const QImage legacyImage = renderOnMagenta(legacy);
        const QImage badgeImage = renderOnMagenta(badge);
        const QSize legacyInk = inkBox(legacyImage);
        const QSize badgeInk = inkBox(badgeImage);
        // 高度必须一致；宽度允许 ±1px（QSS 盒模型多给的那点空白是整数近似）。
        EXPECT_EQ(badgeInk.height(), legacyInk.height()) << item.name;
        EXPECT_LE(std::abs(badgeInk.width() - legacyInk.width()), 1)
            << item.name << " ink " << legacyInk.width() << "x" << legacyInk.height()
            << " vs " << badgeInk.width() << "x" << badgeInk.height();
        // 取色只能取主色：字距/居中方式在自绘和 QSS 之间差 1px，按坐标采会把
        // "笔画 vs 两笔之间"当成配色差异（running/selected 那档就是）。
        const QVector<QColor> legacyColors = dominantColors(legacyImage);
        const QVector<QColor> badgeColors = dominantColors(badgeImage);
        ASSERT_EQ(legacyColors.size(), 2) << item.name;
        ASSERT_EQ(badgeColors.size(), 2) << item.name;
        EXPECT_LE(channelDelta(legacyColors.at(0), badgeColors.at(0)), 3)
            << item.name << " background";
        EXPECT_LE(channelDelta(legacyColors.at(1), badgeColors.at(1)), 3)
            << item.name << " text";

        delete badge;
        delete legacy;
    }
}

TEST(NodeCardTests, nodeGridKeepsZeroStyleSheetsInsideCards)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedNodes(*context, 6);

    ProxyPage page(*context);
    page.resize(900, 700);
    page.selectPanel(ProxyPage::Panel::Nodes);
    page.show();
    ASSERT_TRUE(waitRows(page, 6));

    const auto cards = page.findChildren<QFrame *>(QStringLiteral("NodeCard"));
    ASSERT_EQ(cards.size(), 6);
    int styled = 0;
    for (auto *card : cards) {
        if (!card->styleSheet().isEmpty()) {
            ++styled;
        }
        for (auto *child : card->findChildren<QWidget *>()) {
            if (!child->styleSheet().isEmpty()) {
                ++styled;
            }
        }
    }
    EXPECT_EQ(styled, 0) << "卡片还在按控件挂 CSS：那 20MB 会回来";
}

TEST(NodeCardTests, singleNodeCardKeepsItsContentHeight)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedNodes(*context, 1);

    ProxyPage page(*context);
    page.resize(900, 900);
    page.selectPanel(ProxyPage::Panel::Nodes);
    page.show();
    ASSERT_TRUE(waitRows(page, 1));

    QFrame *card = page.findChild<QFrame *>(QStringLiteral("NodeCard"));
    ASSERT_TRUE(card != nullptr);
    QCoreApplication::processEvents();
    // 旧行为：网格只有一行时那一行吃掉整个视口，卡片被拉成巨物。
    EXPECT_GE(card->height(), 40) << "卡片塌成 0 高了";
    EXPECT_LE(card->height(), card->sizeHint().height() + 24)
        << "单卡高度 " << card->height() << " 远超内容高度 " << card->sizeHint().height();
}

TEST(NodeCardTests, nodeGridPagesFiftyCardsAtATime)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedNodes(*context, 120);

    ProxyPage page(*context);
    page.resize(980, 800);
    page.selectPanel(ProxyPage::Panel::Nodes);
    page.show();
    ASSERT_TRUE(waitRows(page, 120));

    // 120 个节点 → 3 页，常驻卡片只有一页的量。
    EXPECT_EQ(page.nodePageCount(), 3);
    EXPECT_EQ(page.nodePage(), 1);
    EXPECT_EQ(page.nodePageCardCount(), page.nodePageSize());
    EXPECT_EQ(cardNames(page).size(), page.nodePageSize());

    auto *next = pagerButton(page, "PagerNext");
    auto *prev = pagerButton(page, "PagerPrev");
    ASSERT_TRUE(next != nullptr);
    ASSERT_TRUE(prev != nullptr);
    EXPECT_FALSE(prev->isEnabled()) << "第一页不该能再往前翻";
    EXPECT_TRUE(next->isEnabled());

    next->click();
    QCoreApplication::processEvents();
    EXPECT_EQ(page.nodePage(), 2);
    EXPECT_TRUE(prev->isEnabled());
    // 第二页的第一张是排序后的第 51 个（延迟递增播种，顺序可预测）。
    QStringList second = cardNames(page);
    ASSERT_FALSE(second.isEmpty());
    EXPECT_EQ(second.first().toStdString(), "NODE-50");

    for (int i = 0; i < 5; ++i) {
        next->click();
        QCoreApplication::processEvents();
    }
    EXPECT_EQ(page.nodePage(), page.nodePageCount()) << "翻过头要停在末页";
    EXPECT_FALSE(next->isEnabled());
    // 末页只剩 20 张卡（120 - 2*50）。
    EXPECT_EQ(page.nodePageCardCount(), 20);

    QPushButton *pill = nullptr;
    for (auto *button : page.findChildren<QPushButton *>()) {
        if (button->property("region").toString() == QLatin1String("all")) {
            pill = button;
        }
    }
    ASSERT_TRUE(pill != nullptr);
    pill->click();
    QCoreApplication::processEvents();
    EXPECT_EQ(page.nodePage(), 1) << "换筛选要回到第一页，否则会停在空页上";
}

TEST(NodeCardTests, pagerLabelUsesTheRustWordings)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto context = makeContext(directory.path());
    ASSERT_TRUE(context != nullptr);
    seedNodes(*context, 51);

    ProxyPage page(*context);
    page.resize(980, 800);
    page.selectPanel(ProxyPage::Panel::Nodes);
    page.show();
    ASSERT_TRUE(waitRows(page, 51));

    const QString expected = UiText::pagerInfo().arg(1).arg(2).arg(51);
    QStringList labels;
    QLabel *pagerLabel = nullptr;
    for (auto *label : page.findChildren<QLabel *>()) {
        labels.push_back(label->text());
        if (label->text() == expected) {
            pagerLabel = label;
        }
    }
    EXPECT_TRUE(labels.contains(expected)) << qPrintable(expected);
    // 文案对上了还得真在界面上：藏在滚动区外面等于用户看不到页数。
    ASSERT_TRUE(pagerLabel != nullptr);
    EXPECT_TRUE(pagerLabel->isVisibleTo(&page)) << "分页行没显示出来";
    EXPECT_GT(pagerLabel->height(), 0);

    auto *next = pagerButton(page, "PagerNext");
    auto *prev = pagerButton(page, "PagerPrev");
    ASSERT_TRUE(next != nullptr);
    ASSERT_TRUE(prev != nullptr);
    EXPECT_TRUE(next->isVisibleTo(&page));
    EXPECT_EQ(next->text().toStdString(), UiText::pagerNext().toStdString());
    EXPECT_EQ(prev->text().toStdString(), UiText::pagerPrev().toStdString());
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Gui::Theme::apply(Gui::Theme::kDefaultThemeId);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
