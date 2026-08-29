#ifndef NETWORK_MONITOR_H
#define NETWORK_MONITOR_H

#include <QObject>

class NetworkMonitor : public QObject
{
    Q_OBJECT
public:
    static void start();
    static void stop();
    static void checkNetwork();
};

#endif
