#include "logindex.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <limits>

namespace logscope {
namespace {
constexpr qint64 ChunkBytes = 1024 * 1024;
constexpr qsizetype PrefixBytes = 1024;

QByteArray readRawLine(QFile &file, const LineEntry &line, qint64 limit = MaxLineBytes) {
    if (!file.seek(line.offset))
        return {};
    QByteArray bytes = file.read(std::min(line.length, limit));
    if (bytes.endsWith('\r'))
        bytes.chop(1);
    if (line.offset == 0 && bytes.startsWith("\xEF\xBB\xBF"))
        bytes.remove(0, 3);
    return bytes;
}

QString changedError() {
    return QStringLiteral("文件已发生变化，请重新加载后再操作。");
}
} // namespace

bool LogIndex::matchesFile() const {
    const QFileInfo info(path);
    return info.exists() && info.isFile() && info.size() == fileSize
        && info.lastModified() == modified;
}

QString levelName(Level level) {
    static const std::array<QString, LevelCount> names{
        QStringLiteral("TRACE"), QStringLiteral("DEBUG"), QStringLiteral("INFO"),
        QStringLiteral("WARN"), QStringLiteral("ERROR"), QStringLiteral("FATAL"),
        QStringLiteral("OTHER")};
    return names.at(static_cast<size_t>(level));
}

QString levelColor(Level level) {
    static const std::array<QString, LevelCount> colors{
        QStringLiteral("#8494a8"), QStringLiteral("#9cafff"), QStringLiteral("#52d5ad"),
        QStringLiteral("#f2c46f"), QStringLiteral("#ff8c98"), QStringLiteral("#ff647c"),
        QStringLiteral("#b1bdcc")};
    return colors.at(static_cast<size_t>(level));
}

Level detectLevel(const QByteArray &prefix) {
    // Parse a leading log header only; words inside the message must not change severity.
    static const QRegularExpression expression(
        QStringLiteral("^\\s*(?:\\[?\\d{4}-\\d{2}-\\d{2}[ T]\\d{2}:\\d{2}:\\d{2}(?:[.,]\\d+)?(?:Z|[+-]\\d{2}:?\\d{2})?\\]?\\s*)?"
                       "[\\[\\(]?(TRACE|DEBUG|INFO|WARN(?:ING)?|ERROR|FATAL|CRITICAL)"
                       "(?:[\\]\\)\\s:|]|$)"),
        QRegularExpression::CaseInsensitiveOption);
    QString text = QString::fromUtf8(prefix);
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    const auto match = expression.match(text);
    if (!match.hasMatch())
        return Level::Other;
    const QString token = match.captured(1).toUpper();
    if (token == QLatin1String("TRACE")) return Level::Trace;
    if (token == QLatin1String("DEBUG")) return Level::Debug;
    if (token == QLatin1String("INFO")) return Level::Info;
    if (token.startsWith(QLatin1String("WARN"))) return Level::Warn;
    if (token == QLatin1String("ERROR")) return Level::Error;
    return Level::Fatal;
}

QString extractTimestamp(const QString &line) {
    static const QRegularExpression expression(QStringLiteral(
        "^\\s*\\[?(\\d{4}-\\d{2}-\\d{2}[ T]\\d{2}:\\d{2}:\\d{2}(?:[.,]\\d+)?(?:Z|[+-]\\d{2}:?\\d{2})?)"));
    return expression.match(line).captured(1);
}

QString displayMessage(const QString &line) {
    static const QRegularExpression header(QStringLiteral(
        "^\\s*(?:\\[?\\d{4}-\\d{2}-\\d{2}[ T]\\d{2}:\\d{2}:\\d{2}(?:[.,]\\d+)?(?:Z|[+-]\\d{2}:?\\d{2})?\\]?\\s*)?"
        "[\\[\\(]?(?:TRACE|DEBUG|INFO|WARN(?:ING)?|ERROR|FATAL|CRITICAL)"
        "(?:[\\]\\)\\s:|]+|$)"), QRegularExpression::CaseInsensitiveOption);
    const auto match = header.match(line);
    return match.hasMatch() ? line.mid(match.capturedEnd()) : line;
}

QString formatBytes(qint64 bytes) {
    static const QStringList units{QStringLiteral("B"), QStringLiteral("KB"),
                                   QStringLiteral("MB"), QStringLiteral("GB")};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024 && unit < units.size() - 1) { value /= 1024; ++unit; }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', unit == 0 ? 0 : 1).arg(units.at(unit));
}

IndexResult buildIndex(const QString &path, const std::atomic_bool &cancel,
                       const Progress &progress) {
    IndexResult result;
    QElapsedTimer timer;
    timer.start();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("无法打开文件：%1").arg(file.errorString());
        return result;
    }
    const QFileInfo info(file);
    if (!info.isFile()) {
        result.error = QStringLiteral("请选择普通日志文件。");
        return result;
    }
    const QByteArray bom = file.peek(4);
    if (bom.startsWith("\xFF\xFE") || bom.startsWith("\xFE\xFF")
        || bom.startsWith(QByteArray::fromHex("0000feff"))) {
        result.error = QStringLiteral("当前版本支持 UTF-8 日志，请先将 UTF-16/UTF-32 文件转换为 UTF-8。");
        return result;
    }
    auto index = std::make_shared<LogIndex>();
    index->path = info.absoluteFilePath();
    index->fileSize = info.size();
    index->modified = info.lastModified();
    qint64 position = 0;
    qint64 lineStart = 0;
    QByteArray prefix;
    prefix.reserve(PrefixBytes);
    auto appendLine = [&](qint64 end) {
        const qint64 length = end - lineStart;
        if (length > MaxLineBytes) {
            result.error = QStringLiteral("单行超过 8 MB，已停止索引以控制内存占用。");
            return false;
        }
        if (index->lines.size() >= std::numeric_limits<int>::max()) {
            result.error = QStringLiteral("行数超过当前表格模型支持的范围。");
            return false;
        }
        const Level level = detectLevel(prefix);
        index->lines.push_back({lineStart, length, level});
        ++index->counts.at(static_cast<size_t>(level));
        prefix.clear();
        return true;
    };
    while (position < index->fileSize) {
        if (cancel.load(std::memory_order_relaxed)) { result.canceled = true; return result; }
        const QByteArray chunk = file.read(std::min(ChunkBytes, index->fileSize - position));
        if (chunk.isEmpty()) {
            result.error = file.error() == QFile::NoError ? changedError() : file.errorString();
            return result;
        }
        qsizetype start = 0;
        while (start < chunk.size()) {
            if (cancel.load(std::memory_order_relaxed)) { result.canceled = true; return result; }
            const qsizetype newline = chunk.indexOf('\n', start);
            const qsizetype end = newline < 0 ? chunk.size() : newline;
            const qsizetype take = std::min(PrefixBytes - prefix.size(), end - start);
            if (take > 0) prefix.append(chunk.constData() + start, take);
            if (position + end - lineStart > MaxLineBytes) {
                result.error = QStringLiteral("单行超过 8 MB，已停止索引以控制内存占用。");
                return result;
            }
            if (newline < 0) break;
            if (!appendLine(position + newline)) return result;
            lineStart = position + newline + 1;
            start = newline + 1;
        }
        position += chunk.size();
        if (progress) progress(position, index->fileSize);
    }
    if (lineStart < position && !appendLine(position)) return result;
    if (cancel.load(std::memory_order_relaxed)) { result.canceled = true; return result; }
    if (!index->matchesFile()) { result.error = changedError(); return result; }
    index->elapsedMs = timer.elapsed();
    result.index = index;
    return result;
}

SearchResult searchIndex(const IndexPtr &index, const SearchOptions &options,
                         const std::atomic_bool &cancel, const Progress &progress) {
    SearchResult result;
    QElapsedTimer timer;
    timer.start();
    if (cancel.load(std::memory_order_relaxed)) { result.canceled = true; return result; }
    if (!index || !index->matchesFile()) { result.error = changedError(); return result; }
    QRegularExpression expression;
    if (options.regex) {
        expression.setPattern(options.query);
        expression.setPatternOptions(options.caseSensitive
            ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption);
        if (!expression.isValid()) {
            result.error = QStringLiteral("正则表达式无效：%1").arg(expression.errorString());
            return result;
        }
    }
    if (options.query.isEmpty()
        && std::all_of(options.levels.begin(), options.levels.end(), [](bool v) { return v; })) {
        result.allRows = true;
        result.elapsedMs = timer.elapsed();
        return result;
    }
    QFile file(index->path);
    if (!file.open(QIODevice::ReadOnly)) { result.error = file.errorString(); return result; }
    const Qt::CaseSensitivity sensitivity = options.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    for (int row = 0; row < index->lines.size(); ++row) {
        if (cancel.load(std::memory_order_relaxed)) { result.canceled = true; return result; }
        const auto &entry = index->lines.at(row);
        if (options.levels.at(static_cast<size_t>(entry.level))) {
            bool match = options.query.isEmpty();
            if (!match) {
                if (!file.seek(entry.offset)) { result.error = file.errorString(); return result; }
                QByteArray bytes = file.read(entry.length);
                if (bytes.size() != entry.length) { result.error = changedError(); return result; }
                const QString text = QString::fromUtf8(bytes);
                match = options.regex ? expression.match(text).hasMatch()
                                      : text.contains(options.query, sensitivity);
            }
            if (match) result.rows.push_back(row);
        }
        if (progress && (row % 4096 == 0)) progress(row, index->lines.size());
    }
    if (!index->matchesFile()) { result.error = changedError(); return result; }
    if (progress) progress(index->lines.size(), index->lines.size());
    result.elapsedMs = timer.elapsed();
    return result;
}

ExportResult exportLines(const IndexPtr &index, const QVector<int> &rows, bool allRows,
                         const QString &target, const std::atomic_bool &cancel,
                         const Progress &progress) {
    ExportResult result;
    if (cancel.load(std::memory_order_relaxed)) { result.canceled = true; return result; }
    if (!index || !index->matchesFile()) { result.error = changedError(); return result; }
    const QFileInfo targetInfo(target);
    const Qt::CaseSensitivity pathCase =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    if (targetInfo.absoluteFilePath().compare(QFileInfo(index->path).absoluteFilePath(), pathCase) == 0
        || (!targetInfo.canonicalFilePath().isEmpty()
            && targetInfo.canonicalFilePath().compare(QFileInfo(index->path).canonicalFilePath(), pathCase) == 0)) {
        result.error = QStringLiteral("导出文件不能覆盖正在查看的源日志。");
        return result;
    }
    QFile source(index->path);
    QSaveFile output(target);
    if (!source.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
        result.error = !source.isOpen() ? source.errorString() : output.errorString();
        return result;
    }
    const qsizetype count = allRows ? index->lines.size() : rows.size();
    for (qsizetype i = 0; i < count; ++i) {
        if (cancel.load(std::memory_order_relaxed)) {
            output.cancelWriting(); result.canceled = true; return result;
        }
        const int row = allRows ? static_cast<int>(i) : rows.at(i);
        if (row < 0 || row >= index->lines.size()) {
            output.cancelWriting(); result.error = QStringLiteral("导出行索引无效。"); return result;
        }
        const auto &entry = index->lines.at(row);
        if (!source.seek(entry.offset)) {
            output.cancelWriting(); result.error = source.errorString(); return result;
        }
        QByteArray bytes = source.read(entry.length);
        if (bytes.size() != entry.length) {
            output.cancelWriting(); result.error = changedError(); return result;
        }
        if (bytes.endsWith('\r')) bytes.chop(1);
        if (entry.offset == 0 && bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);
        bytes.append('\n');
        if (output.write(bytes) != bytes.size()) {
            output.cancelWriting(); result.error = output.errorString(); return result;
        }
        ++result.count;
        if (progress && (i % 4096 == 0)) progress(i, count);
    }
    if (!index->matchesFile()) { output.cancelWriting(); result.error = changedError(); return result; }
    if (cancel.load(std::memory_order_relaxed)) { output.cancelWriting(); result.canceled = true; return result; }
    if (!output.commit()) result.error = output.errorString();
    return result;
}

QString readLinePreview(const IndexPtr &index, int row, bool *truncated) {
    if (truncated) *truncated = false;
    if (!index || row < 0 || row >= index->lines.size()) return {};
    QFile file(index->path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto &line = index->lines.at(row);
    if (truncated) *truncated = line.length > PreviewBytes;
    return QString::fromUtf8(readRawLine(file, line, PreviewBytes));
}

} // namespace logscope
