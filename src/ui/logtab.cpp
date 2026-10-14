#include "logtab.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace logscope {
LogTab::LogTab(QString path, QWidget *parent)
    : QWidget(parent), m_path(QFileInfo(path).absoluteFilePath()), m_model(new LogModel(this)) {
    qRegisterMetaType<IndexResult>();
    qRegisterMetaType<SearchResult>();
    qRegisterMetaType<ExportResult>();
    setObjectName(QStringLiteral("logTab"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(12);

    auto *searchRow = new QHBoxLayout;
    m_query = new QLineEdit;
    m_query->setObjectName(QStringLiteral("searchInput"));
    m_query->setPlaceholderText(QStringLiteral("搜索日志内容…  Ctrl+F"));
    m_query->setClearButtonEnabled(true);
    m_regex = new QCheckBox(QStringLiteral("正则"));
    m_regex->setObjectName(QStringLiteral("regexToggle"));
    m_case = new QCheckBox(QStringLiteral("区分大小写"));
    searchRow->addWidget(m_query, 1);
    searchRow->addWidget(m_regex);
    searchRow->addWidget(m_case);
    layout->addLayout(searchRow);

    auto *filters = new QHBoxLayout;
    auto *filterLabel = new QLabel(QStringLiteral("日志级别"));
    filterLabel->setObjectName(QStringLiteral("mutedLabel"));
    filters->addWidget(filterLabel);
    for (int i = 0; i < LevelCount; ++i) {
        auto *button = new QToolButton;
        const auto level = static_cast<Level>(i);
        button->setText(levelName(level));
        button->setObjectName(QStringLiteral("level%1").arg(levelName(level)));
        button->setCheckable(true);
        button->setChecked(true);
        button->setStyleSheet(QStringLiteral("QToolButton:checked { color: %1; }").arg(levelColor(level)));
        filters->addWidget(button);
        m_levels.at(static_cast<size_t>(i)) = button;
        connect(button, &QToolButton::toggled, this, [this] { m_searchTimer.start(); });
    }
    filters->addStretch();
    auto *reset = new QPushButton(QStringLiteral("重置筛选"));
    connect(reset, &QPushButton::clicked, this, [this] {
        m_query->clear(); m_regex->setChecked(false); m_case->setChecked(false);
        for (auto *button : m_levels) button->setChecked(true);
        m_searchTimer.start();
    });
    filters->addWidget(reset);
    layout->addLayout(filters);

    m_summary = new QLabel(QStringLiteral("准备读取文件…"));
    m_summary->setObjectName(QStringLiteral("summaryLabel"));
    layout->addWidget(m_summary);

    auto *splitter = new QSplitter(Qt::Vertical);
    m_table = new QTableView;
    m_table->setObjectName(QStringLiteral("logTable"));
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    m_table->setFont(mono);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(32);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setMinimumSectionSize(32);
    m_table->setColumnWidth(LogModel::Bookmark, 36);
    m_table->setColumnWidth(LogModel::LineNumber, 72);
    m_table->setColumnWidth(LogModel::Timestamp, 210);
    m_table->setColumnWidth(LogModel::Severity, 86);
    splitter->addWidget(m_table);

    auto *detailPanel = new QWidget;
    auto *detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(0, 10, 0, 0);
    auto *detailRow = new QHBoxLayout;
    m_detailTitle = new QLabel(QStringLiteral("上下文预览 · 选择一行查看前后内容"));
    detailRow->addWidget(m_detailTitle, 1);
    auto *bookmark = new QPushButton(QStringLiteral("★ 书签  Ctrl+B"));
    auto *next = new QPushButton(QStringLiteral("下一书签"));
    auto *copy = new QPushButton(QStringLiteral("复制当前行"));
    detailRow->addWidget(bookmark);
    detailRow->addWidget(next);
    detailRow->addWidget(copy);
    detailLayout->addLayout(detailRow);
    m_detail = new QPlainTextEdit;
    m_detail->setObjectName(QStringLiteral("contextPreview"));
    m_detail->setReadOnly(true);
    m_detail->setFont(mono);
    m_detail->setPlaceholderText(QStringLiteral("原始行内容将在这里显示，筛选后仍可查看原文件中的相邻行。"));
    detailLayout->addWidget(m_detail);
    splitter->addWidget(detailPanel);
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({460, 180});
    layout->addWidget(splitter, 1);

    auto *stateRow = new QHBoxLayout;
    m_state = new QLabel;
    m_state->setWordWrap(true);
    m_state->setObjectName(QStringLiteral("stateLabel"));
    m_progress = new QProgressBar;
    m_progress->setRange(0, 1000);
    m_progress->setTextVisible(false);
    m_progress->setFixedWidth(150);
    m_progress->hide();
    m_cancelButton = new QPushButton(QStringLiteral("取消任务"));
    m_cancelButton->hide();
    stateRow->addWidget(m_state, 1);
    stateRow->addWidget(m_progress);
    stateRow->addWidget(m_cancelButton);
    layout->addLayout(stateRow);

    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(300);
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(750);
    m_watcher = new QFileSystemWatcher(this);
    m_watcher->addPath(QFileInfo(m_path).absolutePath());
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &LogTab::fileChanged);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        const auto index = m_model->index();
        if ((index && !index->matchesFile()) || (m_stale && QFileInfo::exists(m_path))) fileChanged();
    });
    connect(&m_reloadTimer, &QTimer::timeout, this, [this] {
        if (m_autoReload && m_stale) reload();
    });
    connect(&m_searchTimer, &QTimer::timeout, this, &LogTab::runSearch);
    connect(m_query, &QLineEdit::textChanged, this, [this] { m_searchTimer.start(); });
    connect(m_query, &QLineEdit::returnPressed, this, [this] { m_searchTimer.stop(); runSearch(); });
    connect(m_regex, &QCheckBox::toggled, this, [this] { m_searchTimer.start(); });
    connect(m_case, &QCheckBox::toggled, this, [this] { m_searchTimer.start(); });
    connect(m_cancelButton, &QPushButton::clicked, this, &LogTab::cancelTasks);
    connect(bookmark, &QPushButton::clicked, this, &LogTab::toggleBookmark);
    connect(next, &QPushButton::clicked, this, &LogTab::nextBookmark);
    connect(copy, &QPushButton::clicked, this, [this] {
        const int row = m_model->sourceRow(m_table->currentIndex().row());
        if (row >= 0) {
            bool truncated = false;
            QApplication::clipboard()->setText(readLinePreview(m_model->index(), row, &truncated));
            setState(truncated ? QStringLiteral("已复制前 64 KB；完整内容可通过导出获取。") : QStringLiteral("已复制当前行。"));
        }
    });
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this] { showSelection(); });
    connect(m_table, &QTableView::clicked, this, [this](const QModelIndex &cell) {
        if (cell.column() == LogModel::Bookmark) toggleBookmark();
    });
    auto *bookmarkAction = new QAction(this);
    bookmarkAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    bookmarkAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(bookmarkAction);
    connect(bookmarkAction, &QAction::triggered, this, &LogTab::toggleBookmark);
    QTimer::singleShot(0, this, &LogTab::reload);
}

LogTab::~LogTab() {
    saveBookmarks();
    for (Job *job : m_jobs) job->cancel();
    for (Job *job : m_jobs) job->wait();
}

void LogTab::setState(const QString &text, bool error) {
    m_state->setText(text);
    m_state->setStyleSheet(error ? QStringLiteral("color: #ff8c98;") : QStringLiteral("color: #8596ac;"));
    emit statusChanged(text);
}

void LogTab::track(Job *job) {
    m_jobs.insert(job);
    connect(job, &QThread::finished, this, [this, job] { m_jobs.remove(job); job->deleteLater(); });
}

void LogTab::showProgress(Job *job, quint64 token, const QString &verb) {
    m_progress->setValue(0);
    m_progress->show();
    m_cancelButton->show();
    setState(verb + QStringLiteral("…"));
    connect(job, &Job::progress, this, [this, token, verb](qint64 done, qint64 total) {
        if (token != m_token) return;
        const int value = total == 0 ? 1000 : static_cast<int>(static_cast<double>(done) / total * 1000);
        m_progress->setValue(value);
        setState(QStringLiteral("%1… %2%").arg(verb).arg(value / 10));
    });
}

void LogTab::cancelTasks() {
    ++m_token;
    m_searchTimer.stop();
    for (Job *job : m_jobs) job->cancel();
    m_loading = false;
    m_progress->hide();
    m_cancelButton->hide();
    setState(QStringLiteral("任务已取消，保留上次完成的结果。"));
}
void LogTab::reload() {
    saveBookmarks();
    cancelTasks();
    m_loading = true;
    m_stale = false;
    const quint64 token = m_token;
    m_model->setIndex(nullptr);
    m_detail->clear();
    m_summary->setText(QStringLiteral("正在建立行索引 · %1").arg(m_path));
    if (QFileInfo::exists(m_path) && !m_watcher->files().contains(m_path)) m_watcher->addPath(m_path);
    auto *job = new IndexJob(m_path, this);
    track(job);
    showProgress(job, token, QStringLiteral("建立索引"));
    connect(job, &IndexJob::completed, this, [this, token](const IndexResult &result) {
        if (token != m_token) return;
        m_loading = false;
        m_progress->hide();
        m_cancelButton->hide();
        if (!result.error.isEmpty() || result.canceled || !result.index) {
            setState(result.error.isEmpty() ? QStringLiteral("索引已取消。") : result.error, true);
            m_summary->setText(QStringLiteral("文件未加载 · 点击重新加载重试"));
            emit indexFinished(false);
            return;
        }
        if (!result.index->matchesFile()) { fileChanged(); emit indexFinished(false); return; }
        m_model->setIndex(result.index);
        loadBookmarks();
        updateSummary();
        setState(QStringLiteral("索引完成 · %1 ms · 选择一行查看上下文").arg(result.index->elapsedMs));
        emit indexFinished(true);
        const auto options = searchOptions();
        if (!options.query.isEmpty() || !std::all_of(options.levels.begin(), options.levels.end(), [](bool v) { return v; }))
            runSearch();
    });
    job->start();
}

SearchOptions LogTab::searchOptions() const {
    SearchOptions options;
    options.query = m_query->text();
    options.regex = m_regex->isChecked();
    options.caseSensitive = m_case->isChecked();
    for (int i = 0; i < LevelCount; ++i) options.levels.at(static_cast<size_t>(i)) = m_levels.at(static_cast<size_t>(i))->isChecked();
    return options;
}

void LogTab::runSearch() {
    if (!ready()) return;
    cancelTasks();
    const quint64 token = m_token;
    auto *job = new SearchJob(m_model->index(), searchOptions(), this);
    track(job);
    showProgress(job, token, QStringLiteral("搜索日志"));
    connect(job, &SearchJob::completed, this, [this, token](SearchResult result) {
        if (token != m_token) return;
        m_progress->hide();
        m_cancelButton->hide();
        if (!result.error.isEmpty() || result.canceled) {
            setState(result.error.isEmpty() ? QStringLiteral("搜索已取消。") : result.error, true);
            emit searchFinished(false);
            return;
        }
        m_model->setRows(std::move(result.rows), result.allRows);
        m_detail->clear();
        m_detailTitle->setText(QStringLiteral("上下文预览 · 选择一行查看前后内容"));
        updateSummary();
        setState(QStringLiteral("搜索完成 · %1 个匹配 · %2 ms").arg(QLocale().toString(m_model->rowCount())).arg(result.elapsedMs));
        emit searchFinished(true);
    });
    job->start();
}

void LogTab::exportTo(const QString &target) {
    if (!ready()) { setState(QStringLiteral("请先完成文件索引。"), true); return; }
    cancelTasks();
    const quint64 token = m_token;
    auto *job = new ExportJob(m_model->index(), m_model->rows(), m_model->allRows(), target, this);
    track(job);
    showProgress(job, token, QStringLiteral("导出当前结果"));
    connect(job, &ExportJob::completed, this, [this, token, target](const ExportResult &result) {
        if (token != m_token) return;
        m_progress->hide();
        m_cancelButton->hide();
        const bool success = result.error.isEmpty() && !result.canceled;
        setState(success ? QStringLiteral("已导出 %1 行 → %2").arg(result.count).arg(target)
                         : result.error.isEmpty() ? QStringLiteral("导出已取消。") : result.error, !success);
        emit exportFinished(success);
    });
    job->start();
}

void LogTab::updateSummary() {
    const auto index = m_model->index();
    if (!index) return;
    m_summary->setText(QStringLiteral("%1  /  %2 行    ·    %3    ·    UTF-8    ·    索引 %4 ms")
        .arg(QLocale().toString(m_model->rowCount())).arg(QLocale().toString(index->lines.size()))
        .arg(formatBytes(index->fileSize)).arg(index->elapsedMs));
    for (int i = 0; i < LevelCount; ++i) {
        auto *button = m_levels.at(static_cast<size_t>(i));
        button->setText(QStringLiteral("%1  %2").arg(levelName(static_cast<Level>(i)))
            .arg(QLocale().toString(index->counts.at(static_cast<size_t>(i)))));
    }
}

void LogTab::showSelection() {
    const auto index = m_model->index();
    const int row = m_model->sourceRow(m_table->currentIndex().row());
    if (!index || row < 0) { m_detail->clear(); return; }
    m_detailTitle->setText(QStringLiteral("原文件第 %1 行 · 字节偏移 %2 · 前后各 2 行")
        .arg(row + 1).arg(index->lines.at(row).offset));
    QStringList lines;
    for (int i = std::max(0, row - 2); i <= std::min(static_cast<int>(index->lines.size()) - 1, row + 2); ++i) {
        bool truncated = false;
        const QString text = readLinePreview(index, i, &truncated);
        lines << QStringLiteral("%1 %2 │ %3%4").arg(i == row ? QStringLiteral("▶") : QStringLiteral(" "))
            .arg(i + 1, 7).arg(text).arg(truncated ? QStringLiteral(" … [预览截断，完整内容可导出]") : QString());
    }
    m_detail->setPlainText(lines.join('\n'));
}

QString LogTab::bookmarkKey() const {
    const auto index = m_model->index();
    if (!index) return {};
    const QByteArray identity = index->path.toUtf8() + ':' + QByteArray::number(index->fileSize)
        + ':' + QByteArray::number(index->modified.toMSecsSinceEpoch());
    return QStringLiteral("bookmarks/%1").arg(QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex()));
}

void LogTab::saveBookmarks() {
    const QString key = bookmarkKey();
    if (key.isEmpty()) return;
    QVariantList values;
    for (qint64 offset : m_model->bookmarks()) values << offset;
    QSettings().setValue(key, values);
}

void LogTab::loadBookmarks() {
    QSet<qint64> offsets;
    const auto values = QSettings().value(bookmarkKey()).toList();
    for (const auto &value : values) offsets.insert(value.toLongLong());
    m_model->setBookmarks(std::move(offsets));
}

void LogTab::toggleBookmark() {
    const int row = m_table->currentIndex().row();
    if (m_model->sourceRow(row) < 0) return;
    const bool added = m_model->toggleBookmark(row);
    saveBookmarks();
    setState(added ? QStringLiteral("已添加书签。") : QStringLiteral("已移除书签。"));
}

void LogTab::nextBookmark() {
    const auto index = m_model->index();
    if (!index || m_model->bookmarks().isEmpty()) { setState(QStringLiteral("尚未添加书签。")); return; }
    QVector<int> visible;
    // Look up bookmarked offsets instead of scanning every line of a large file.
    for (qint64 offset : m_model->bookmarks()) {
        const auto it = std::lower_bound(index->lines.cbegin(), index->lines.cend(), offset,
            [](const LineEntry &entry, qint64 value) { return entry.offset < value; });
        if (it == index->lines.cend() || it->offset != offset) continue;
        const int row = m_model->visibleRow(static_cast<int>(it - index->lines.cbegin()));
        if (row >= 0) visible.push_back(row);
    }
    if (visible.isEmpty()) { setState(QStringLiteral("书签被当前筛选隐藏，请先重置筛选。")); return; }
    std::sort(visible.begin(), visible.end());
    auto it = std::upper_bound(visible.cbegin(), visible.cend(), m_table->currentIndex().row());
    if (it == visible.cend()) it = visible.cbegin();
    m_table->setCurrentIndex(m_model->index(*it, LogModel::Message));
    m_table->scrollTo(m_table->currentIndex());
}

void LogTab::setAutoReload(bool enabled) {
    m_autoReload = enabled;
    if (enabled && m_stale) m_reloadTimer.start();
    if (!enabled) m_reloadTimer.stop();
}

void LogTab::fileChanged() {
    saveBookmarks();
    cancelTasks();
    m_stale = true;
    m_model->setIndex(nullptr);
    m_detail->clear();
    m_summary->setText(QStringLiteral("源文件已变化 · 原索引已失效"));
    setState(m_autoReload ? QStringLiteral("检测到文件变化，等待文件稳定后自动重载…")
                          : QStringLiteral("文件已变化，请点击重新加载。"), !m_autoReload);
    if (m_autoReload) m_reloadTimer.start();
}

void LogTab::focusSearch() { m_query->setFocus(); m_query->selectAll(); }
} // namespace logscope
