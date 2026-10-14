#include "logmodel.h"
#include <QColor>
#include <QFontDatabase>
#include <algorithm>

namespace logscope {
LogModel::LogModel(QObject *parent) : QAbstractTableModel(parent) {}

int LogModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() || !m_index ? 0
        : m_allRows ? static_cast<int>(m_index->lines.size()) : static_cast<int>(m_rows.size());
}
int LogModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : ColumnCount; }

int LogModel::sourceRow(int visibleRow) const {
    return visibleRow < 0 || visibleRow >= rowCount() ? -1 : m_allRows ? visibleRow : m_rows.at(visibleRow);
}

int LogModel::visibleRow(int source) const {
    if (!m_index || source < 0 || source >= m_index->lines.size()) return -1;
    if (m_allRows) return source;
    const auto it = std::lower_bound(m_rows.cbegin(), m_rows.cend(), source);
    return it != m_rows.cend() && *it == source ? static_cast<int>(it - m_rows.cbegin()) : -1;
}

QString LogModel::lineText(int row) const {
    if (const QString *cached = m_cache.object(row)) return *cached;
    if (!m_file.isOpen()) return {};
    const auto &line = m_index->lines.at(row);
    if (!m_file.seek(line.offset)) return {};
    QByteArray bytes = m_file.read(std::min<qint64>(line.length, 2048));
    if (bytes.endsWith('\r')) bytes.chop(1);
    if (line.offset == 0 && bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);
    QString text = QString::fromUtf8(bytes);
    if (line.length > 2048) text += QStringLiteral(" …");
    m_cache.insert(row, new QString(text), std::max(1, static_cast<int>(text.size() * 2)));
    return text;
}

QVariant LogModel::data(const QModelIndex &cell, int role) const {
    if (!cell.isValid() || cell.column() >= ColumnCount) return {};
    const int row = sourceRow(cell.row());
    if (row < 0) return {};
    const auto &entry = m_index->lines.at(row);
    if (role == Qt::DisplayRole) {
        switch (cell.column()) {
        case Bookmark: return m_bookmarks.contains(entry.offset) ? QStringLiteral("★") : QString();
        case LineNumber: return row + 1;
        case Timestamp: return extractTimestamp(lineText(row));
        case Severity: return levelName(entry.level);
        case Message: return displayMessage(lineText(row));
        default: return {};
        }
    }
    if (role == Qt::ForegroundRole) {
        if (cell.column() == Severity) return QColor(levelColor(entry.level));
        if (cell.column() == Bookmark) return QColor(QStringLiteral("#f2c46f"));
        if (cell.column() == LineNumber || cell.column() == Timestamp) return QColor(QStringLiteral("#8596ac"));
    }
    if (role == Qt::TextAlignmentRole && cell.column() != Message)
        return static_cast<int>(Qt::AlignVCenter | (cell.column() == LineNumber ? Qt::AlignRight : Qt::AlignLeft));
    if (role == Qt::ToolTipRole && cell.column() == Bookmark)
        return QStringLiteral("点击添加 / 移除书签（Ctrl+B）");
    return {};
}

QVariant LogModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    static const QStringList headers{QStringLiteral("★"), QStringLiteral("行号"),
        QStringLiteral("时间"), QStringLiteral("级别"), QStringLiteral("消息")};
    return headers.value(section);
}

void LogModel::setIndex(IndexPtr index) {
    beginResetModel();
    m_file.close();
    m_index = std::move(index);
    m_rows.clear();
    m_bookmarks.clear();
    m_allRows = true;
    m_cache.clear();
    if (m_index) { m_file.setFileName(m_index->path); m_file.open(QIODevice::ReadOnly); }
    endResetModel();
}

void LogModel::setRows(QVector<int> rows, bool allRows) {
    beginResetModel();
    m_rows = std::move(rows);
    m_allRows = allRows;
    endResetModel();
}

void LogModel::setBookmarks(QSet<qint64> bookmarks) {
    m_bookmarks = std::move(bookmarks);
    if (rowCount() > 0) emit dataChanged(this->createIndex(0, Bookmark), this->createIndex(rowCount() - 1, Bookmark));
}

bool LogModel::toggleBookmark(int visible) {
    const int row = sourceRow(visible);
    if (row < 0) return false;
    const qint64 offset = m_index->lines.at(row).offset;
    const bool added = !m_bookmarks.contains(offset);
    if (added) m_bookmarks.insert(offset); else m_bookmarks.remove(offset);
    emit dataChanged(createIndex(visible, Bookmark), createIndex(visible, Bookmark));
    return added;
}
} // namespace logscope
