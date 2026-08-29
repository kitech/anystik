#ifndef PHONE_MONITOR_H
#define PHONE_MONITOR_H

#include <QObject>
#include <QString>

class PhoneMonitor : public QObject
{
    Q_OBJECT
public:
    static void start();
    static void stop();
    static PhoneMonitor* instance();
    static int answerMode();
    static void setAnswerMode(int mode);

Q_SIGNALS:
    void incomingCall(const QString& phoneNumber);

private:
    PhoneMonitor() = default;
};

#endif
