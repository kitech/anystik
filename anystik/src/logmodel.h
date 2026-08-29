#ifndef LOG_MODEL_H
#define LOG_MODEL_H

#include <QObject>
#include <QString>
#include <deque>

class LogModel : public QObject
{
    Q_OBJECT
public:
    enum Level { Debug, Info, Warn, Error };

    struct Entry {
        Level level;
        QString timestamp;
        QString tag;
        QString message;
    };

    static LogModel& instance();

    void append(Level level, const QString& tag, const QString& message);
    void clear();

    int count() const { return m_buffer.size(); }
    const Entry& at(int i) const { return m_buffer[i]; }
    const std::deque<Entry>& entries() const { return m_buffer; }

signals:
    void entryAdded(int index);
    void cleared();

private:
    LogModel(QObject* parent = nullptr);

    std::deque<Entry> m_buffer;
    int m_maxEntries = 500;
};

#endif
