#include "core/logindex.h"
#include "ui/logmodel.h"
#include "ui/logtab.h"
#include "ui/mainwindow.h"
#include <QCheckBox>
#include <QFile>
#include <QLineEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTableView>
#include <QTemporaryDir>
#include <QtTest>

using namespace logscope;

class LogScopeTests : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    std::atomic_bool m_cancel{false};
    QString write(const QString &name, const QByteArray &bytes) {
        const QString path = m_dir.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
        file.close();
        return path;
    }
private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("LogScopeTests"));
        QCoreApplication::setApplicationName(QStringLiteral("LogScopeTests"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
        QStandardPaths::setTestModeEnabled(true);
    }
    void indexBoundaryAndUnicode() {
        QByteArray first("[INFO] ");
        first.append(QByteArray(1024 * 1024 - 8, 'x'));
        const QByteArray second = QStringLiteral("2026-10-13 10:00:00 [ERROR] 中文错误").toUtf8();
        const QString path = write(QStringLiteral("中文路径.log"), first + "\r\n" + second);
        const auto result = buildIndex(path, m_cancel);
        QVERIFY2(result.index != nullptr, qPrintable(result.error));
        QCOMPARE(result.index->lines.size(), 2);
        QCOMPARE(result.index->lines.at(0).length, first.size() + 1);
        QCOMPARE(result.index->lines.at(1).offset, first.size() + 2);
        QCOMPARE(result.index->lines.at(1).level, Level::Error);
        QCOMPARE(readLinePreview(result.index, 1), QString::fromUtf8(second));
        bool truncated = false;
        readLinePreview(result.index, 0, &truncated);
        QVERIFY(truncated);
        SearchOptions options;
        options.query = QStringLiteral("中文错误");
        const auto found = searchIndex(result.index, options, m_cancel);
        QCOMPARE(found.rows, QVector<int>{1});
    }
    void bomEmptyAndLineEndings() {
        const auto empty = buildIndex(write(QStringLiteral("empty.log"), {}), m_cancel);
        QVERIFY(empty.index);
        QCOMPARE(empty.index->lines.size(), 0);
        const auto bom = buildIndex(write(QStringLiteral("bom.log"), QByteArray::fromHex("efbbbf") + "[INFO] one\r\n\n[ERROR] two\n"), m_cancel);
        QVERIFY(bom.index);
        QCOMPARE(bom.index->lines.size(), 3);
        QCOMPARE(bom.index->lines.at(0).level, Level::Info);
        QCOMPARE(readLinePreview(bom.index, 0), QStringLiteral("[INFO] one"));
        QCOMPARE(readLinePreview(bom.index, 1), QString());
        QCOMPARE(readLinePreview(bom.index, 2), QStringLiteral("[ERROR] two"));
    }
    void severityFromHeaderOnly_data() {
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<int>("expected");
        QTest::newRow("iso") << QByteArray("2026-10-13T09:41:01.123Z [WARN] timeout") << int(Level::Warn);
        QTest::newRow("lower") << QByteArray("[error] disk full") << int(Level::Error);
        QTest::newRow("warning") << QByteArray("WARNING: slow service") << int(Level::Warn);
        QTest::newRow("critical") << QByteArray("CRITICAL service stopped") << int(Level::Fatal);
        QTest::newRow("message-word") << QByteArray("[INFO] error field is optional") << int(Level::Info);
        QTest::newRow("stack") << QByteArray("    at ErrorHandler::run()") << int(Level::Other);
        QTest::newRow("word-boundary") << QByteArray("INFORMATION message") << int(Level::Other);
    }
    void severityFromHeaderOnly() {
        QFETCH(QByteArray, text);
        QFETCH(int, expected);
        QCOMPARE(int(detectLevel(text)), expected);
    }
    void cancellationAndInputErrors() {
        std::atomic_bool cancel{true};
        const QString path = write(QStringLiteral("cancel.log"), "[INFO] a\n");
        QVERIFY(buildIndex(path, cancel).canceled);
        QVERIFY(!buildIndex(m_dir.filePath(QStringLiteral("missing.log")), m_cancel).error.isEmpty());
        QVERIFY(!buildIndex(write(QStringLiteral("utf16.log"), QByteArray::fromHex("fffe4100")), m_cancel).error.isEmpty());
        QVERIFY(!buildIndex(write(QStringLiteral("oversized.log"), QByteArray(MaxLineBytes + 1, 'a')), m_cancel).error.isEmpty());
        QByteArray lines;
        for (int i = 0; i < 25000; ++i) lines += "[DEBUG] sample\n";
        std::atomic_bool during{false};
        const auto result = buildIndex(write(QStringLiteral("during.log"), lines), during,
            [&](qint64, qint64) { during.store(true); });
        QVERIFY(result.canceled);
    }
    void searchFiltersAndRegex() {
        const auto index = buildIndex(write(QStringLiteral("search.log"),
            "[INFO] timeout\n[ERROR] Timeout #42\n[ERROR] timeout #43\n[DEBUG] success\n"), m_cancel).index;
        QVERIFY(index);
        SearchOptions options;
        options.query = QStringLiteral("timeout");
        QCOMPARE(searchIndex(index, options, m_cancel).rows, (QVector<int>{0, 1, 2}));
        options.caseSensitive = true;
        QCOMPARE(searchIndex(index, options, m_cancel).rows, (QVector<int>{0, 2}));
        options.levels.fill(false);
        options.levels.at(int(Level::Error)) = true;
        options.caseSensitive = false;
        QCOMPARE(searchIndex(index, options, m_cancel).rows, (QVector<int>{1, 2}));
        options.regex = true;
        options.query = QStringLiteral("#4[23]$");
        QCOMPARE(searchIndex(index, options, m_cancel).rows, (QVector<int>{1, 2}));
        options.query = QStringLiteral("[");
        QVERIFY(!searchIndex(index, options, m_cancel).error.isEmpty());
        QVERIFY(searchIndex(index, SearchOptions{}, m_cancel).allRows);
        std::atomic_bool cancel{true};
        QVERIFY(searchIndex(index, SearchOptions{}, cancel).canceled);
        QFile file(index->path);
        QVERIFY(file.open(QIODevice::Append));
        file.write("[INFO] changed\n"); file.close();
        QVERIFY(!searchIndex(index, SearchOptions{}, m_cancel).error.isEmpty());
    }
    void exportPreservesUnicodeAndDoesNotOverwriteSource() {
        const QByteArray content = QStringLiteral("[INFO] 中文\r\n[ERROR] 错误\r\n").toUtf8();
        const auto index = buildIndex(write(QStringLiteral("export.log"), content), m_cancel).index;
        QVERIFY(index);
        const QString target = m_dir.filePath(QStringLiteral("selected.log"));
        const auto result = exportLines(index, {1}, false, target, m_cancel);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.count, 1);
        QFile file(target);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QStringLiteral("[ERROR] 错误\n").toUtf8());
        file.close();
        QVERIFY(!exportLines(index, {}, true, index->path, m_cancel).error.isEmpty());
#ifdef Q_OS_WIN
        QVERIFY(!exportLines(index, {}, true, index->path.toUpper(), m_cancel).error.isEmpty());
#endif
        QFile source(index->path);
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), content);
        std::atomic_bool cancel{true};
        QVERIFY(exportLines(index, {}, true, target, cancel).canceled);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QStringLiteral("[ERROR] 错误\n").toUtf8());
    }
    void modelMapsFilteredRowsAndBookmarks() {
        const auto index = buildIndex(write(QStringLiteral("model.log"), "[INFO] a\n[ERROR] b\n[WARN] c\n"), m_cancel).index;
        LogModel model;
        model.setIndex(index);
        QCOMPARE(model.rowCount(), 3);
        QVERIFY(model.toggleBookmark(1));
        model.setRows({1}, false);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0, LogModel::LineNumber)).toInt(), 2);
        QCOMPARE(model.data(model.index(0, LogModel::Bookmark)).toString(), QStringLiteral("★"));
        QCOMPARE(model.visibleRow(2), -1);
        QCOMPARE(model.visibleRow(1), 0);
    }
    void uiSearchExportReloadAndDuplicateTabs() {
        MainWindow window;
        const QString path = write(QStringLiteral("ui.log"), "[INFO] start\n[ERROR] timeout\n[DEBUG] end\n");
        LogTab *tab = window.openFile(path);
        QCOMPARE(window.openFile(path), tab);
        QTRY_VERIFY_WITH_TIMEOUT(tab->ready(), 5000);
        auto *query = tab->findChild<QLineEdit *>(QStringLiteral("searchInput"));
        QVERIFY(query);
        QSignalSpy search(tab, &LogTab::searchFinished);
        query->setText(QStringLiteral("timeout"));
        QTRY_COMPARE_WITH_TIMEOUT(search.count(), 1, 5000);
        QCOMPARE(tab->model()->rowCount(), 1);
        QSignalSpy exported(tab, &LogTab::exportFinished);
        tab->exportTo(m_dir.filePath(QStringLiteral("ui-selected.log")));
        QTRY_COMPARE_WITH_TIMEOUT(exported.count(), 1, 5000);
        QVERIFY(exported.first().first().toBool());
        search.clear();
        tab->findChild<QCheckBox *>(QStringLiteral("regexToggle"))->setChecked(true);
        query->setText(QStringLiteral("["));
        QTRY_COMPARE_WITH_TIMEOUT(search.count(), 1, 5000);
        QVERIFY(!search.first().first().toBool());
        QCOMPARE(tab->model()->rowCount(), 1); // Keep the last valid result on regex errors.
        query->clear();
        tab->findChild<QCheckBox *>(QStringLiteral("regexToggle"))->setChecked(false);
        QTRY_COMPARE_WITH_TIMEOUT(tab->model()->rowCount(), 3, 5000);
        QFile file(path);
        QVERIFY(file.open(QIODevice::Append)); file.write("[WARN] new row\n"); file.close();
        QTRY_VERIFY_WITH_TIMEOUT(!tab->ready(), 5000);
        tab->setAutoReload(true);
        QTRY_VERIFY_WITH_TIMEOUT(tab->ready(), 5000);
        QCOMPARE(tab->model()->rowCount(), 4);
    }
    void closingActiveDocumentIsSafe() {
        QByteArray content;
        for (int i = 0; i < 80000; ++i) content += "[INFO] running task\n";
        auto *tab = new LogTab(write(QStringLiteral("close.log"), content));
        QCoreApplication::processEvents();
        delete tab; // Cancels and joins jobs before document-owned objects are destroyed.
    }
    void supersededSearchDoesNotReplaceNewResult() {
        QByteArray content;
        for (int i = 0; i < 200000; ++i) content += "[INFO] alpha\n";
        content += "[ERROR] omega\n";
        LogTab tab(write(QStringLiteral("superseded.log"), content));
        QTRY_VERIFY_WITH_TIMEOUT(tab.ready(), 5000);
        auto *query = tab.findChild<QLineEdit *>(QStringLiteral("searchInput"));
        QSignalSpy finished(&tab, &LogTab::searchFinished);
        query->setText(QStringLiteral("alpha"));
        QVERIFY(QMetaObject::invokeMethod(query, "returnPressed", Qt::DirectConnection));
        query->setText(QStringLiteral("omega"));
        QVERIFY(QMetaObject::invokeMethod(query, "returnPressed", Qt::DirectConnection));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(tab.model()->rowCount(), 1);
        QCOMPARE(tab.model()->sourceRow(0), 200000);
    }
};

QTEST_MAIN(LogScopeTests)
#include "test_logscope.moc"
