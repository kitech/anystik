#include "logmodel.h"
#include <QTime>

LogModel& LogModel::instance()
{
    static LogModel s_instance;
    return s_instance;
}

LogModel::LogModel(QObject* parent)
    : QObject(parent)
{
}

void LogModel::append(Level level, const QString& tag, const QString& message)
{
    QString ts = QTime::currentTime().toString("HH:mm:ss.zzz");
    m_buffer.push_back({level, ts, tag, message});
    if (m_buffer.size() > m_maxEntries) {
        m_buffer.pop_front();
    }
    emit entryAdded(m_buffer.size() - 1);
}

void LogModel::clear()
{
    m_buffer.clear();
    emit cleared();
}
