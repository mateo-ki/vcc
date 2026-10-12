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

    /// 建议名只取 host；带日期占位符的模板要先按拉取时同一套规则展开，否则 QUrl 解析不出
    /// host（实测原串 `urlHost()` 为空）。存库的仍是原模板，每次拉取按当天重算。
    static QString hostForSuggestedName(const QString &urlTemplate);

    /// "URL 模板 ?" 上那段占位符说明。单独拎出来是给守卫用的：提示里列的写法必须和
    /// Parser::expandUrlTemplate 真的认的那批严格一致（tests/guiSourcesPage）。
    static const QString &urlTemplateHelp();

signals:
    void statusMessageRequested(const QString &message);
    /// 一次单源拉取真的解析出了节点（拉取失败或 0 节点不发）。外壳据此决定"拉完自动测速"。
    void nodesFetchedForSource(const QString &sourceId, int nodeCount);

public slots:
    void openCreateForm();
    void openEditForm(const Models::Source &source);
    void importFromClipboard();
    void exportToClipboard();
    void fetchAllEnabled();
    void fetchAllViaProxy();
    void fetchOne(const QString &sourceId);
    void fetchOneViaProxy(const QString &sourceId);
    /// 拉取单源之后只重写它那一行（其余行的控件对象保持不变）。定位不到就退回整表刷新。
    void refreshSourceRow(const QString &sourceId);
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
    /// 写源表的整一行（6 列，其中 4 列是 setCellWidget 容器）。整表重建和单行刷新共用这一份。
    void writeSourceRow(int row, const Models::Source &source, bool proxyAvailable);
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
