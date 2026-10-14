#pragma once
#include "logindex.h"
#include <QThread>

namespace logscope {

class Job : public QThread {
    Q_OBJECT
public:
    explicit Job(QObject *parent = nullptr) : QThread(parent) {}
    void cancel() { m_cancel.store(true, std::memory_order_relaxed); }
signals:
    void progress(qint64 completed, qint64 total);
protected:
    std::atomic_bool m_cancel{false};
    Progress progressCallback();
};

class IndexJob final : public Job {
    Q_OBJECT
public:
    IndexJob(QString path, QObject *parent) : Job(parent), m_path(std::move(path)) {}
signals:
    void completed(logscope::IndexResult result);
protected:
    void run() override;
private:
    QString m_path;
};

class SearchJob final : public Job {
    Q_OBJECT
public:
    SearchJob(IndexPtr index, SearchOptions options, QObject *parent)
        : Job(parent), m_index(std::move(index)), m_options(std::move(options)) {}
signals:
    void completed(logscope::SearchResult result);
protected:
    void run() override;
private:
    IndexPtr m_index;
    SearchOptions m_options;
};

class ExportJob final : public Job {
    Q_OBJECT
public:
    ExportJob(IndexPtr index, QVector<int> rows, bool allRows, QString target, QObject *parent)
        : Job(parent), m_index(std::move(index)), m_rows(std::move(rows)),
          m_allRows(allRows), m_target(std::move(target)) {}
signals:
    void completed(logscope::ExportResult result);
protected:
    void run() override;
private:
    IndexPtr m_index;
    QVector<int> m_rows;
    bool m_allRows;
    QString m_target;
};

} // namespace logscope
