#ifndef KEEP_ALIVE_H
#define KEEP_ALIVE_H

#include <QObject>

class KeepAlive : public QObject
{
    Q_OBJECT
public:
    static void start();
    static void stop();
};

#endif
