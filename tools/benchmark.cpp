#include "core/logindex.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#endif

using namespace logscope;

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Reproducible LogScope index/search benchmark"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Existing UTF-8 log file"));
    QCommandLineOption generate(QStringLiteral("generate"), QStringLiteral("Generate a deterministic log file, then benchmark it"), QStringLiteral("file"));
    QCommandLineOption lines(QStringLiteral("lines"), QStringLiteral("Number of generated lines"), QStringLiteral("count"), QStringLiteral("100000"));
    QCommandLineOption query(QStringLiteral("query"), QStringLiteral("Literal search query"), QStringLiteral("text"), QStringLiteral("timeout"));
    parser.addOptions({generate, lines, query});
    parser.process(app);
    QString path = parser.isSet(generate) ? parser.value(generate) : parser.positionalArguments().value(0);
    QTextStream out(stdout);
    QTextStream err(stderr);
    if (path.isEmpty()) { parser.showHelp(1); }
    if (parser.isSet(generate)) {
        bool valid = false;
        const qint64 count = parser.value(lines).toLongLong(&valid);
        if (!valid || count < 1 || count > 100000000) { err << "--lines must be between 1 and 100000000\n"; return 1; }
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) { err << file.errorString() << '\n'; return 1; }
        QByteArray buffer;
        buffer.reserve(2 * 1024 * 1024);
        for (qint64 i = 0; i < count; ++i) {
            const char *level = i % 100 == 0 ? "ERROR" : i % 10 == 0 ? "WARN" : "INFO";
            buffer += "2026-10-13 10:00:00.123 [";
            buffer += level;
            buffer += "] request=";
            buffer += QByteArray::number(i);
            buffer += i % 100 == 0 ? " timeout exceeded; " : " operation completed; ";
            buffer += "module=ImportService worker=4 customer=2048 elapsed=125ms payload=abcdefghijklmnopqrstuvwxyz0123456789\n";
            if (buffer.size() >= 1024 * 1024) {
                if (file.write(buffer) != buffer.size()) { err << file.errorString() << '\n'; return 1; }
                buffer.clear();
            }
        }
        if (file.write(buffer) != buffer.size() || !file.commit()) { err << file.errorString() << '\n'; return 1; }
    }
    std::atomic_bool cancel{false};
    const auto indexed = buildIndex(path, cancel);
    if (!indexed.index) { err << indexed.error << '\n'; return 2; }
    SearchOptions options;
    options.query = parser.value(query);
    const auto searched = searchIndex(indexed.index, options, cancel);
    if (!searched.error.isEmpty()) { err << searched.error << '\n'; return 3; }
    QJsonObject report;
    report[QStringLiteral("file")] = indexed.index->path;
    report[QStringLiteral("file_size_bytes")] = indexed.index->fileSize;
    report[QStringLiteral("lines")] = indexed.index->lines.size();
    report[QStringLiteral("index_ms")] = indexed.index->elapsedMs;
    report[QStringLiteral("search_ms")] = searched.elapsedMs;
    report[QStringLiteral("matches")] = searched.allRows ? indexed.index->lines.size() : searched.rows.size();
    report[QStringLiteral("query")] = options.query;
    report[QStringLiteral("index_capacity_bytes")] = static_cast<qint64>(indexed.index->lines.capacity() * sizeof(LineEntry));
    report[QStringLiteral("qt_version")] = QString::fromLatin1(qVersion());
#ifdef _MSC_VER
    report[QStringLiteral("compiler")] = QStringLiteral("MSVC %1").arg(_MSC_VER);
#endif
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        report[QStringLiteral("peak_working_set_bytes")] = static_cast<qint64>(counters.PeakWorkingSetSize);
#endif
    out << QJsonDocument(report).toJson(QJsonDocument::Indented);
    return 0;
}
