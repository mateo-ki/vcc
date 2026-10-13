#pragma once

#include <QDialog>
#include <QString>
#include <QVector>
#include <functional>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QVBoxLayout;
QT_END_NAMESPACE

namespace App {
class AppContext;
}

namespace Gui {

struct CommandPaletteHost {
    std::function<void(int tabIndex)> selectTab;
    std::function<void(const QString &nodeId)> startNode;
};

struct PaletteItem {
    enum class Kind { Action, Node, Source, Navigation };
    Kind kind = Kind::Action;
    QString title;
    QString subtitle;
    QString icon;
    QString payload;
    std::function<void()> action;
};

class CommandPalette : public QDialog {
    Q_OBJECT

public:
    explicit CommandPalette(App::AppContext &context, CommandPaletteHost host, QWidget *parent = nullptr);
    ~CommandPalette() override = default;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onSearchTextChanged(const QString &text);
    void onItemActivated(QListWidgetItem *item);

private:
    void buildDefaultItems();
    void renderItems(const QVector<PaletteItem> &items);
    void applyPaletteTheme();

    App::AppContext &context_;
    CommandPaletteHost host_;
    QLineEdit *searchEdit_ = nullptr;
    QListWidget *listWidget_ = nullptr;
    QVector<PaletteItem> allItems_;
    QVector<PaletteItem> filteredItems_;
};

}
