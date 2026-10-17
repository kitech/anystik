#ifndef DAVSYNC_PROBERECV_H
#define DAVSYNC_PROBERECV_H

// stikcommon/e2e/proberecv.h —— e2e 探针的 SyncEngine 信号接收器。
//
// 为什么单独一个 moc 头：Qt3 的 QObject::connect 只认 SIGNAL()/SLOT() 字符串，
// 而 stikcommon/qconnect_slots.h 的 functor connect 只覆盖 QNetworkReply*/
// QNetworkAccessManager* 发送者，覆盖不到 SyncEngine。故探针用一个带 Q_OBJECT
// 的接收者，走字符串连接。（DAV_SYNC_JOIN_CONNECT 只是 `using ::connect;`，
// 同样只解决 QNetworkReply 发送者。）

#ifdef QT3_BUILD
#include <qobject.h>
#include <qstring.h>
#else
#include <QObject>
#include <QString>
#endif

class ProbeRecv : public QObject
{
    Q_OBJECT
public:
    ProbeRecv()
        : finished(false), finCount(0), exitCode(-1), prog100(0),
          treeFiles(-1), treeDirs(-1) {}

    bool finished;
    int  finCount;
    int  exitCode;
    int  prog100;
    int  treeFiles;
    int  treeDirs;

public slots:
    void onProgress(int percent, const QString& step, const QString& detail)
    {
        (void)step; (void)detail;
        if (percent == 100) {
            ++prog100;
        }
    }
    void onFinished(int code, const QString& summary)
    {
        (void)summary;
        finished = true;
        exitCode = code;
        ++finCount;
    }
    void onTree(int files, int dirs)
    {
        treeFiles = files;
        treeDirs = dirs;
    }
    void onLog(int level, const QString& tag, const QString& line);
};

#endif // DAVSYNC_PROBERECV_H
