#pragma once
#include "core/logindex.h"
#include <QAbstractTableModel>
#include <QCache>
#include <QFile>
#include <QSet>

namespace logscope {
class LogModel final : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Bookmark, LineNumber, Timestamp, Severity, Message, ColumnCount };
    using QAbstractTableModel::index;
    explicit LogModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void setIndex(IndexPtr index);
    void setRows(QVector<int> rows, bool allRows);
    void setBookmarks(QSet<qint64> bookmarks);
    bool toggleBookmark(int visibleRow);
    int sourceRow(int visibleRow) const;
    int visibleRow(int sourceRow) const;
    IndexPtr index() const { return m_index; }
    const QVector<int> &rows() const { return m_rows; }
    bool allRows() const { return m_allRows; }
    const QSet<qint64> &bookmarks() const { return m_bookmarks; }
private:
    QString lineText(int sourceRow) const;
    IndexPtr m_index;
    QVector<int> m_rows;
    bool m_allRows = true;
    QSet<qint64> m_bookmarks;
    mutable QFile m_file;
    mutable QCache<int, QString> m_cache{4 * 1024 * 1024};
};
} // namespace logscope
