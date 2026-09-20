#ifndef PHONE_MONITOR_H
#define PHONE_MONITOR_H

#include <QObject>
#include <QString>
#include <QDateTime>

struct PhoneCallRecord
{
    QString state;      // RINGING / OFFHOOK / IDLE ...
    QString number;
    qint64 timestamp = 0;
};

struct SmsRecord
{
    QString sender;
    QString body;
    qint64 timestamp = 0;
};

class PhoneMonitor : public QObject
{
    Q_OBJECT
public:
    static void start();
    static void stop();
    static void requestPermissions(bool force);
    static PhoneMonitor* instance();
    static int answerMode();
    static void setAnswerMode(int mode);

    int callCount() const;
    int smsCount() const;
    const QList<PhoneCallRecord>& callRecords() const;
    const QList<SmsRecord>& smsRecords() const;

    void addCallEvent(const QString& state, const QString& number);
    void addSmsEvent(const QString& sender, const QString& body);

Q_SIGNALS:
    void incomingCall(const QString& phoneNumber);
    void countersChanged();
    void callRecorded();
    void smsRecorded();

private:
    PhoneMonitor() = default;

    QList<PhoneCallRecord> m_calls;
    QList<SmsRecord> m_sms;
};

#endif