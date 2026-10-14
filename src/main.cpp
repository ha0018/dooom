#include "ui/mainwindow.h"
#include "ui/logtab.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QItemSelectionModel>
#include <QTableView>
#include <QTimer>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LogScope"));
    QCoreApplication::setApplicationName(QStringLiteral("LogScope"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setPointSize(10);
    app.setFont(font);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("LogScope — 本地日志分析工作台"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("要打开的 UTF-8 日志文件"), QStringLiteral("[files…]"));
    QCommandLineOption demo(QStringLiteral("demo"), QStringLiteral("打开内置示例"));
    QCommandLineOption screenshot(QStringLiteral("screenshot"), QStringLiteral("加载示例并保存界面截图后退出"), QStringLiteral("path"));
    parser.addOption(demo);
    parser.addOption(screenshot);
    parser.process(app);
    logscope::MainWindow window;
    if (parser.isSet(screenshot)) window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    for (const auto &path : parser.positionalArguments()) window.openFile(path);
    logscope::LogTab *tab = nullptr;
    if (parser.isSet(demo) || parser.isSet(screenshot)) tab = window.openDemo();
    if (parser.isSet(screenshot)) {
        if (!tab) return 2;
        QObject::connect(tab, &logscope::LogTab::indexFinished, &window, [&](bool success) {
            if (!success) { app.exit(2); return; }
            QTimer::singleShot(200, &window, [&] {
                auto *table = tab->findChild<QTableView *>(QStringLiteral("logTable"));
                if (table && table->model()->rowCount() > 0) {
                    const int row = std::min(8, table->model()->rowCount() - 1);
                    table->setCurrentIndex(table->model()->index(row, 4));
                    table->selectionModel()->select(table->model()->index(row, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                }
                const QString output = QFileInfo(parser.value(screenshot)).absoluteFilePath();
                QDir().mkpath(QFileInfo(output).absolutePath());
                const bool saved = window.grab().save(output);
                app.exit(saved ? 0 : 3);
            });
        });
        QTimer::singleShot(30000, &app, [&] { app.exit(4); });
    }
    return app.exec();
}
