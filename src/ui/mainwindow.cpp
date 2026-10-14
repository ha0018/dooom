#include "mainwindow.h"
#include "logtab.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

namespace logscope {
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("LogScope · 日志分析工作台"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/logscope.svg")));
    setMinimumSize(1024, 700);
    resize(1400, 900);
    setAcceptDrops(true);
    QFile stylesheet(QStringLiteral(":/theme.qss"));
    if (stylesheet.open(QIODevice::ReadOnly)) setStyleSheet(QString::fromUtf8(stylesheet.readAll()));

    auto *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    auto *open = fileMenu->addAction(QStringLiteral("打开日志…"));
    open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, &MainWindow::chooseFiles);
    auto *demo = fileMenu->addAction(QStringLiteral("打开示例日志"));
    connect(demo, &QAction::triggered, this, [this] { openDemo(); });
    auto *exportAction = fileMenu->addAction(QStringLiteral("导出当前结果…"));
    exportAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    connect(exportAction, &QAction::triggered, this, &MainWindow::exportCurrent);
    fileMenu->addSeparator();
    auto *close = fileMenu->addAction(QStringLiteral("关闭标签"));
    close->setShortcut(QKeySequence::Close);
    connect(close, &QAction::triggered, this, [this] { closeTab(m_tabs->currentIndex()); });
    auto *quit = fileMenu->addAction(QStringLiteral("退出"));
    connect(quit, &QAction::triggered, this, &QWidget::close);
    auto *viewMenu = menuBar()->addMenu(QStringLiteral("查看(&V)"));
    auto *find = viewMenu->addAction(QStringLiteral("搜索日志"));
    find->setShortcut(QKeySequence::Find);
    connect(find, &QAction::triggered, this, [this] { if (currentTab()) currentTab()->focusSearch(); });
    auto *reload = viewMenu->addAction(QStringLiteral("重新加载"));
    reload->setShortcut(QKeySequence(QStringLiteral("F5")));
    connect(reload, &QAction::triggered, this, [this] { if (currentTab()) currentTab()->reload(); });
    m_autoReload = viewMenu->addAction(QStringLiteral("文件变化后自动重载"));
    m_autoReload->setCheckable(true);
    m_autoReload->setChecked(QSettings().value(QStringLiteral("autoReload"), false).toBool());
    connect(m_autoReload, &QAction::toggled, this, [this](bool enabled) {
        QSettings().setValue(QStringLiteral("autoReload"), enabled);
        for (int i = 0; i < m_tabs->count(); ++i) static_cast<LogTab *>(m_tabs->widget(i))->setAutoReload(enabled);
    });
    auto *help = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    auto *about = help->addAction(QStringLiteral("关于 LogScope"));
    connect(about, &QAction::triggered, this, [this] {
        QMessageBox::about(this, QStringLiteral("LogScope 0.1"),
            QStringLiteral("面向开发者的本地日志分析工具\n\nUTF-8 · 按需读取 · 后台搜索 · 安全导出\n"
                           "支持普通 UTF-8 文本；单行上限 8 MB。\n自动重载会重新建立完整索引。"));
    });

    auto *root = new QWidget;
    auto *rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    auto *sidebar = new QFrame;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(230);
    auto *rail = new QVBoxLayout(sidebar);
    rail->setContentsMargins(20, 28, 20, 20);
    rail->setSpacing(14);
    auto *brand = new QLabel(QStringLiteral("◈  LOGSCOPE"));
    brand->setObjectName(QStringLiteral("brand"));
    rail->addWidget(brand);
    auto *tagline = new QLabel(QStringLiteral("让每一行日志都有线索"));
    tagline->setObjectName(QStringLiteral("mutedLabel"));
    rail->addWidget(tagline);
    rail->addSpacing(20);
    auto *openButton = new QPushButton(QStringLiteral("＋  打开日志"));
    openButton->setObjectName(QStringLiteral("primaryButton"));
    openButton->setMinimumHeight(44);
    rail->addWidget(openButton);
    connect(openButton, &QPushButton::clicked, this, &MainWindow::chooseFiles);
    auto *demoButton = new QPushButton(QStringLiteral("体验示例日志  →"));
    rail->addWidget(demoButton);
    connect(demoButton, &QPushButton::clicked, this, [this] { openDemo(); });
    rail->addSpacing(18);
    auto *recentTitle = new QLabel(QStringLiteral("最近打开"));
    recentTitle->setObjectName(QStringLiteral("sectionLabel"));
    rail->addWidget(recentTitle);
    m_recent = new QListWidget;
    m_recent->setObjectName(QStringLiteral("recentFiles"));
    m_recent->setWordWrap(true);
    m_recent->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rail->addWidget(m_recent, 1);
    connect(m_recent, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        openFile(item->data(Qt::UserRole).toString());
    });
    auto *privacy = new QLabel(QStringLiteral("●  本地文件分析\n\nCtrl+O  打开文件\nCtrl+F  搜索日志\nCtrl+B  添加书签\nCtrl+E  导出结果"));
    privacy->setObjectName(QStringLiteral("railFooter"));
    rail->addWidget(privacy);
    rootLayout->addWidget(sidebar);

    auto *workspace = new QWidget;
    auto *workLayout = new QVBoxLayout(workspace);
    workLayout->setContentsMargins(0, 0, 0, 0);
    workLayout->setSpacing(0);
    auto *header = new QFrame;
    header->setObjectName(QStringLiteral("workspaceHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(24, 22, 24, 22);
    auto *titles = new QVBoxLayout;
    auto *title = new QLabel(QStringLiteral("日志分析工作台"));
    title->setObjectName(QStringLiteral("pageTitle"));
    titles->addWidget(title);
    auto *subtitle = new QLabel(QStringLiteral("快速定位问题，保留完整上下文"));
    subtitle->setObjectName(QStringLiteral("mutedLabel"));
    titles->addWidget(subtitle);
    headerLayout->addLayout(titles, 1);
    auto *reloadButton = new QPushButton(QStringLiteral("重新加载  F5"));
    auto *exportButton = new QPushButton(QStringLiteral("导出当前结果"));
    headerLayout->addWidget(reloadButton);
    headerLayout->addWidget(exportButton);
    connect(reloadButton, &QPushButton::clicked, this, [this] { if (currentTab()) currentTab()->reload(); });
    connect(exportButton, &QPushButton::clicked, this, &MainWindow::exportCurrent);
    workLayout->addWidget(header);
    m_stack = new QStackedWidget;
    auto *empty = new QWidget;
    auto *emptyLayout = new QVBoxLayout(empty);
    emptyLayout->setContentsMargins(70, 60, 70, 60);
    emptyLayout->addStretch();
    auto *emptyIcon = new QLabel(QStringLiteral("⌕"));
    emptyIcon->setObjectName(QStringLiteral("emptyIcon"));
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyIcon);
    auto *emptyTitle = new QLabel(QStringLiteral("把日志拖到这里，开始分析"));
    emptyTitle->setObjectName(QStringLiteral("emptyTitle"));
    emptyTitle->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyTitle);
    auto *emptyText = new QLabel(QStringLiteral("打开 UTF-8 日志，搜索关键词、筛选错误、标记关键线索。\n文件内容按需读取，后台任务可随时取消。"));
    emptyText->setAlignment(Qt::AlignCenter);
    emptyText->setObjectName(QStringLiteral("mutedLabel"));
    emptyLayout->addWidget(emptyText);
    auto *tryButton = new QPushButton(QStringLiteral("打开示例，体验完整流程"));
    tryButton->setObjectName(QStringLiteral("primaryButton"));
    tryButton->setFixedWidth(250);
    emptyLayout->addSpacing(20);
    emptyLayout->addWidget(tryButton, 0, Qt::AlignCenter);
    connect(tryButton, &QPushButton::clicked, this, [this] { openDemo(); });
    emptyLayout->addStretch();
    m_stack->addWidget(empty);
    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_stack->addWidget(m_tabs);
    workLayout->addWidget(m_stack, 1);
    rootLayout->addWidget(workspace, 1);
    setCentralWidget(root);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    statusBar()->showMessage(QStringLiteral("就绪 · 打开日志文件或体验示例"));
    refreshRecent();
    const QByteArray geometry = QSettings().value(QStringLiteral("windowGeometry")).toByteArray();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
}
LogTab *MainWindow::currentTab() const {
    return qobject_cast<LogTab *>(m_tabs->currentWidget());
}

void MainWindow::chooseFiles() {
    const QStringList paths = QFileDialog::getOpenFileNames(this, QStringLiteral("打开日志文件"),
        QSettings().value(QStringLiteral("lastDirectory")).toString(),
        QStringLiteral("日志文件 (*.log *.txt *.jsonl);;所有文件 (*)"));
    for (const auto &path : paths) openFile(path);
}

LogTab *MainWindow::openFile(const QString &path) {
    const QFileInfo info(path);
    const QString absolute = info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto *tab = static_cast<LogTab *>(m_tabs->widget(i));
        if (tab->path() == absolute) { m_tabs->setCurrentIndex(i); return tab; }
    }
    auto *tab = new LogTab(absolute);
    tab->setAutoReload(m_autoReload->isChecked());
    const int position = m_tabs->addTab(tab, info.fileName());
    m_tabs->setTabToolTip(position, absolute);
    m_tabs->setCurrentIndex(position);
    m_stack->setCurrentWidget(m_tabs);
    connect(tab, &LogTab::statusChanged, this, [this, tab](const QString &status) {
        if (currentTab() == tab) statusBar()->showMessage(status);
    });
    connect(tab, &LogTab::indexFinished, this, [this, absolute](bool success) {
        if (success) rememberFile(absolute);
    });
    if (info.exists()) QSettings().setValue(QStringLiteral("lastDirectory"), info.absolutePath());
    return tab;
}

LogTab *MainWindow::openDemo() {
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!QDir().mkpath(directory)) {
        statusBar()->showMessage(QStringLiteral("无法创建示例日志目录。")); return nullptr;
    }
    const QString target = directory + QStringLiteral("/demo.log");
    if (!QFileInfo::exists(target)) {
        QFile source(QStringLiteral(":/demo.log"));
        QSaveFile output(target);
        if (!source.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
            statusBar()->showMessage(QStringLiteral("无法读取示例日志。")); return nullptr;
        }
        const QByteArray bytes = source.readAll();
        if (output.write(bytes) != bytes.size() || !output.commit()) {
            statusBar()->showMessage(QStringLiteral("无法保存示例日志。")); return nullptr;
        }
    }
    return openFile(target);
}

void MainWindow::exportCurrent() {
    auto *tab = currentTab();
    if (!tab || !tab->ready()) { statusBar()->showMessage(QStringLiteral("请先打开并完成索引。")); return; }
    const QString target = QFileDialog::getSaveFileName(this, QStringLiteral("导出当前筛选结果"),
        QFileInfo(tab->path()).absolutePath() + QStringLiteral("/filtered.log"), QStringLiteral("日志文件 (*.log);;文本文件 (*.txt)"));
    if (!target.isEmpty()) tab->exportTo(target);
}

void MainWindow::closeTab(int index) {
    if (index < 0 || index >= m_tabs->count()) return;
    QWidget *tab = m_tabs->widget(index);
    m_tabs->removeTab(index);
    delete tab;
    if (m_tabs->count() == 0) m_stack->setCurrentIndex(0);
}

void MainWindow::rememberFile(const QString &path) {
    QSettings settings;
    QStringList recent = settings.value(QStringLiteral("recentFiles")).toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 10) recent.removeLast();
    settings.setValue(QStringLiteral("recentFiles"), recent);
    refreshRecent();
}

void MainWindow::refreshRecent() {
    m_recent->clear();
    const QStringList recent = QSettings().value(QStringLiteral("recentFiles")).toStringList();
    for (const QString &path : recent) {
        auto *item = new QListWidgetItem(QFileInfo(path).fileName(), m_recent);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        item->setSizeHint(QSize(180, 44));
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) {
        for (const QUrl &url : event->mimeData()->urls())
            if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()) { event->acceptProposedAction(); return; }
    }
}

void MainWindow::dropEvent(QDropEvent *event) {
    for (const QUrl &url : event->mimeData()->urls())
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()) openFile(url.toLocalFile());
    event->acceptProposedAction();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    QSettings().setValue(QStringLiteral("windowGeometry"), saveGeometry());
    while (m_tabs->count() > 0) closeTab(0);
    QMainWindow::closeEvent(event);
}
} // namespace logscope
