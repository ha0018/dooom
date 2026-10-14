#include "jobs.h"
#include <QElapsedTimer>

namespace logscope {
Progress Job::progressCallback() {
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    return [this, timer](qint64 done, qint64 total) {
        if (timer->elapsed() >= 60 || done == total) {
            emit progress(done, total);
            timer->restart();
        }
    };
}

void IndexJob::run() { emit completed(buildIndex(m_path, m_cancel, progressCallback())); }
void SearchJob::run() { emit completed(searchIndex(m_index, m_options, m_cancel, progressCallback())); }
void ExportJob::run() { emit completed(exportLines(m_index, m_rows, m_allRows, m_target, m_cancel, progressCallback())); }
} // namespace logscope
