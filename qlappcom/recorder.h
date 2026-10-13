#ifndef RECORDER_H
#define RECORDER_H

#include "compat34.h"
#ifdef QT3_BUILD
#include <qtimer.h>
#else
#include <QTimer>
#endif

// 两段式媒体录制（录音/录屏）：start() fork+exec ffmpeg 后台录制，stop() SIGINT 回收，
// 完成后发 finished()。空文件/非零退出码由调用方按"为空处理"。
// 约定：不抛异常、不 try-catch，所有失败经返回值和信号传递。
class MediaRecorder : public QObject {
    Q_OBJECT
public:
    MediaRecorder(QObject* parent = 0);
    virtual ~MediaRecorder();

    enum Kind { kNone = 0, kAudio = 1, kScreen = 2 };

    bool start(int kind, QString* outFile);   // false=ffmpeg缺失/无DISPLAY/设备不可用
    void stop();                              // 幂等：无录制直接返回
    bool isActive() const { return m_pid > 0; }

signals:
    void finished(const QString& file, int exitCode);   // WIFEXITED?WEXITSTATUS:-1；大小由调用方查

private slots:
    void onPoll();

private:
    int m_pid;
    QString m_file;
    QTimer* m_poll;
};

#endif