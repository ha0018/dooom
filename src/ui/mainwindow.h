#pragma once
#include <QMainWindow>

class QTabWidget;
class QListWidget;
class QStackedWidget;
class QAction;
class QDragEnterEvent;
class QDropEvent;
class QCloseEvent;

namespace logscope {
class LogTab;
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    LogTab *openFile(const QString &path);
    LogTab *openDemo();
    LogTab *currentTab() const;
protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
private:
    void chooseFiles();
    void exportCurrent();
    void closeTab(int index);
    void refreshRecent();
    void rememberFile(const QString &path);
    QTabWidget *m_tabs;
    QListWidget *m_recent;
    QStackedWidget *m_stack;
    QAction *m_autoReload;
};
} // namespace logscope
