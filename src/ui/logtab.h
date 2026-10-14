#pragma once
#include "core/jobs.h"
#include "logmodel.h"
#include <QWidget>
#include <QTimer>
#include <QSet>
#include <array>

class QLineEdit;
class QCheckBox;
class QToolButton;
class QLabel;
class QProgressBar;
class QPushButton;
class QTableView;
class QPlainTextEdit;
class QFileSystemWatcher;

namespace logscope {
class LogTab final : public QWidget {
    Q_OBJECT
public:
    explicit LogTab(QString path, QWidget *parent = nullptr);
    ~LogTab() override;
    QString path() const { return m_path; }
    bool ready() const { return m_model->index() != nullptr && !m_loading; }
    LogModel *model() const { return m_model; }
    void reload();
    void exportTo(const QString &target);
    void cancelTasks();
    void setAutoReload(bool enabled);
    void focusSearch();
signals:
    void statusChanged(QString text);
    void indexFinished(bool success);
    void searchFinished(bool success);
    void exportFinished(bool success);
private:
    void runSearch();
    SearchOptions searchOptions() const;
    void track(Job *job);
    void showProgress(Job *job, quint64 token, const QString &verb);
    void updateSummary();
    void showSelection();
    void toggleBookmark();
    void nextBookmark();
    void saveBookmarks();
    void loadBookmarks();
    QString bookmarkKey() const;
    void fileChanged();
    void setState(const QString &text, bool error = false);
    QString m_path;
    LogModel *m_model;
    QTableView *m_table;
    QPlainTextEdit *m_detail;
    QLineEdit *m_query;
    QCheckBox *m_regex;
    QCheckBox *m_case;
    std::array<QToolButton *, LevelCount> m_levels{};
    QLabel *m_summary;
    QLabel *m_state;
    QLabel *m_detailTitle;
    QProgressBar *m_progress;
    QPushButton *m_cancelButton;
    QFileSystemWatcher *m_watcher;
    QTimer m_searchTimer;
    QTimer m_reloadTimer;
    QSet<Job *> m_jobs;
    quint64 m_token = 0;
    bool m_loading = false;
    bool m_autoReload = false;
    bool m_stale = false;
};
} // namespace logscope
