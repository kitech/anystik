#include "recorder.h"
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#ifdef QT3_BUILD
#include <qstringlist.h>
#include <qfileinfo.h>
#else
#include <QStringList>
#include <QFileInfo>
#endif

MediaRecorder::MediaRecorder(QObject* parent)
    : QObject(parent), m_pid(0), m_poll(0) {
    m_poll = new QTimer(this);
    connect(m_poll, SIGNAL(timeout()), this, SLOT(onPoll()));
    qTimerStart(m_poll, 100, true);
    m_poll->stop();
}

MediaRecorder::~MediaRecorder() {
    if (m_pid > 0) {
        ::kill(m_pid, SIGINT);
        ::waitpid(m_pid, 0, 0);
        m_pid = 0;
    }
}

bool MediaRecorder::start(int kind, QString* outFile) {
    if (m_pid > 0) { return false; }
    char* argv[16];
    QByteArray store[16];
    int argc = 0;
    argv[argc++] = (char*)"ffmpeg";
    QString file;
    QStringList a;
    if (kind == kAudio) {
        file = "/tmp/qlapp_audio_" + QString::number((long)::time(0))
             + "_" + QString::number((long)::getpid()) + ".aac";
        a << "-y" << "-f" << "alsa" << "-i" << "default" << "-c:a" << "aac" << file;
    } else if (kind == kScreen) {
        QString display = QString::fromLocal8Bit(::getenv("DISPLAY"));
        if (display.isEmpty()) { return false; }
        file = "/tmp/qlapp_screen_" + QString::number((long)::time(0))
             + "_" + QString::number((long)::getpid()) + ".mp4";
        a << "-y" << "-f" << "x11grab" << "-i" << display
          << "-c:v" << "libx264" << "-preset" << "ultrafast" << "-pix_fmt" << "yuv420p"
          << "-movflags" << "frag_keyframe+empty_moov" << file;
    } else {
        return false;
    }
    for (int i = 0; i < a.size(); i++) {
#ifdef QT3_BUILD
        store[argc] = a[i].local8Bit();
#else
        store[argc] = a[i].toLocal8Bit();
#endif
        argv[argc] = store[argc].data();
        ++argc;
    }
    argv[argc] = 0;
    pid_t pid = ::fork();
    if (pid < 0) { return false; }
    if (pid == 0) {
        ::setsid();
        int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            ::dup2(devnull, 1);
            ::dup2(devnull, 2);
            if (devnull > 2) { ::close(devnull); }
        }
        ::execvp(argv[0], argv);
        ::_exit(127);
    }
    m_pid = (int)pid;
    m_file = file;
    if (outFile) { *outFile = file; }
    qTimerStart(m_poll, 100, true);
    return true;
}

void MediaRecorder::stop() {
    if (m_pid <= 0) { return; }
    ::kill(m_pid, SIGINT);   // ffmpeg 收到 SIGINT 会收尾写尾并退出
}

void MediaRecorder::onPoll() {
    if (m_pid <= 0) {
        m_poll->stop();
        return;
    }
    int st = 0;
    pid_t r = ::waitpid(m_pid, &st, WNOHANG);
    if (r <= 0) { return; }   // 仍在录制
    m_poll->stop();
    int code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    QString f = m_file;
    m_pid = 0;
#ifdef QT3_BUILD
    m_file = QString();
#else
    m_file.clear();
#endif
    emit finished(f, code);
}
