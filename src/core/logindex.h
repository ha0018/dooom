#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>
#include <array>
#include <atomic>
#include <functional>
#include <memory>

namespace logscope {

enum class Level : quint8 { Trace, Debug, Info, Warn, Error, Fatal, Other, Count };
constexpr int LevelCount = static_cast<int>(Level::Count);
constexpr qint64 MaxLineBytes = 8 * 1024 * 1024;
constexpr qint64 PreviewBytes = 64 * 1024;

struct LineEntry {
    qint64 offset = 0;
    qint64 length = 0;
    Level level = Level::Other;
};

struct LogIndex {
    QString path;
    qint64 fileSize = 0;
    QDateTime modified;
    QVector<LineEntry> lines;
    std::array<qint64, LevelCount> counts{};
    qint64 elapsedMs = 0;
    bool matchesFile() const;
};

using IndexPtr = std::shared_ptr<const LogIndex>;
using Progress = std::function<void(qint64, qint64)>;

struct IndexResult {
    IndexPtr index;
    QString error;
    bool canceled = false;
};

struct SearchOptions {
    QString query;
    bool regex = false;
    bool caseSensitive = false;
    std::array<bool, LevelCount> levels{true, true, true, true, true, true, true};
};

struct SearchResult {
    QVector<int> rows;
    bool allRows = false;
    QString error;
    bool canceled = false;
    qint64 elapsedMs = 0;
};

struct ExportResult {
    QString error;
    bool canceled = false;
    qint64 count = 0;
};

QString levelName(Level level);
QString levelColor(Level level);
Level detectLevel(const QByteArray &prefix);
QString extractTimestamp(const QString &line);
QString displayMessage(const QString &line);
QString formatBytes(qint64 bytes);
IndexResult buildIndex(const QString &path, const std::atomic_bool &cancel,
                       const Progress &progress = {});
SearchResult searchIndex(const IndexPtr &index, const SearchOptions &options,
                         const std::atomic_bool &cancel, const Progress &progress = {});
ExportResult exportLines(const IndexPtr &index, const QVector<int> &rows, bool allRows,
                         const QString &target, const std::atomic_bool &cancel,
                         const Progress &progress = {});
QString readLinePreview(const IndexPtr &index, int row, bool *truncated = nullptr);

} // namespace logscope

Q_DECLARE_METATYPE(logscope::IndexResult)
Q_DECLARE_METATYPE(logscope::SearchResult)
Q_DECLARE_METATYPE(logscope::ExportResult)
