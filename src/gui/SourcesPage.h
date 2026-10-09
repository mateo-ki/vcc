#pragma once

#include "gui/Page.h"
#include "models/Source.h"
#include "models/Types.h"

#include <QDialog>
#include <QList>
#include <QMap>
#include <QString>
#include <QVector>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
QT_END_NAMESPACE

namespace Gui {

/// 订阅源管理页，对应 Rust `gui/views/sources.rs` 的 `ui_sources`。
class SourcesPage : public Page {
    Q_OBJECT

public:
    explicit SourcesPage(App::AppContext &context, QWidget *parent = nullptr);

    void refresh() override;

    int sourceCount() const { return sources_.size(); }
    int fetchResultCount() const { return fetchResults_.size(); }

signals:
    void statusMessageRequested(const QString &message);

public slots:
    void openCreateForm();
    void openEditForm(const Models::Source &source);
    void importFromClipboard();
    void exportToClipboard();
    void fetchAllEnabled();
    void fetchAllViaProxy();
    void fetchOne(const QString &sourceId);
    void fetchOneViaProxy(const QString &sourceId);
    void toggleSourceEnabled(const QString &sourceId, bool enabled);
    void toggleSourcePush(const QString &sourceId, bool pushDisabled);
    void deleteSource(const QString &sourceId);
    void analyzeHomogeneity();
    void discoverGists();

private:
    QWidget *buildHeader();
    QWidget *buildActionBar();
    QWidget *buildGistBar();
    QWidget *buildSourcesTable();
    QWidget *buildResultsTable();

    void reloadSources();
    void populateSourcesTable();
    void populateResultsTable();
    std::optional<QString> subscriptionProxyUrl() const;

    // 数据
    QVector<Models::Source> sources_;
    QVector<Models::FetchResult> fetchResults_;
    QMap<QString, qint64> groupAliveCounts_;
    QVector<Models::SourceHomogeneityReport> homogeneityReports_;

    // 面板切换
    QPushButton *listTabBtn_ = nullptr;
    QPushButton *resultsTabBtn_ = nullptr;
    QStackedWidget *subPanels_ = nullptr;

    // Gist 发现
    QVector<QCheckBox *> gistQueryChecks_;
    QSpinBox *gistPagesSpin_ = nullptr;
    QPushButton *discoverGistsBtn_ = nullptr;

    // 动作按钮
    QPushButton *addBtn_ = nullptr;
    QPushButton *importBtn_ = nullptr;
    QPushButton *exportBtn_ = nullptr;
    QPushButton *fetchAllBtn_ = nullptr;
    QPushButton *fetchProxyBtn_ = nullptr;
    QPushButton *refreshBtn_ = nullptr;
    QPushButton *pruneBtn_ = nullptr;
    QLabel *autoFetchLabel_ = nullptr;

    // 表格
    QTableWidget *sourcesTable_ = nullptr;
    QTableWidget *resultsTable_ = nullptr;

    bool busy_ = false;
    bool homogeneityBusy_ = false;
};

} // namespace Gui
