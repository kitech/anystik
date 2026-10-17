// stikcommon/e2e/davsync_e2e.cpp —— 真 WebDAV(wsgidav) 上的 SyncEngine 端到端探针。
//
// 断言：
//   1) finished(exitCode=0) 恰好一次；treeScanned 有发射；progress 到达 100
//   2) 「动词/路径序列」：OPTIONS 先于 PROPFIND
//   3) 新增普通文件：PUT /anystik/pastes/regular.png
//   4) 可合并边车：GET /anystik/pastes/_stikmeta.svg 先于 PUT 同一路径（折叠合并）
//   5) 全程无 MOVE、无 .conflict（可合并边车不走双保留）
//   6) 合并后云端 _stikmeta.svg = 本地∪云端（含两侧 rel）
//
// 用法：davsync_e2e <baseUrl>   例如 http://127.0.0.1:8098
// 环境：DAVSYNC_LOCAL_BASE（本地库 base，含 pastes/regular.png 与 pastes/_stikmeta.svg）
//       DAVSYNC_LOG（访问日志路径，由 logmw.py 写）
//       XDG_DATA_HOME（隔离 baseline 存储）

#ifdef QT3_BUILD
#include <qapplication.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qfile.h>
#include <qtextstream.h>
#include <qfileinfo.h>
#include <qdatetime.h>
#else
#include <QApplication>
#include <QString>
#include <QStringList>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDateTime>
#endif

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <unistd.h>

#include "qcoreapplication_shim.h"
#include "qstring_shim.h"
#include "eventpoller.h"
#include "qwebdavtransport.h"
#include "davlocalsource.h"
#include "davbisync.h"
#include "proberecv.h"

namespace {

#ifdef QT3_BUILD
static const int kReadOnly = IO_ReadOnly;
#else
static const int kReadOnly = QIODevice::ReadOnly;
#endif

int g_fails = 0;
void check(bool ok, const char* what)
{
    std::fprintf(stderr, "%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        ++g_fails;
    }
}

QString envStr(const char* k, const QString& dflt)
{
    const char* v = std::getenv(k);
    return (v && *v) ? QString::fromUtf8(v) : dflt;
}

// 把 baseUrl 解析为 host / port。
void parseBase(const QString& baseUrl, QString* host, int* port)
{
    QString s = baseUrl;
    const int schemePos = s.find(QStringLiteral("://"));
    if (schemePos >= 0) {
        s = s.mid(schemePos + 3);
    }
    QString hostPart = s;
    const int slash = s.find(QLatin1Char('/'));
    if (slash >= 0) {
        hostPart = s.left(slash);
    }
    const int colon = hostPart.find(QLatin1Char(':'));
    if (colon >= 0) {
        *host = hostPart.left(colon);
        *port = hostPart.mid(colon + 1).toInt();
    } else {
        *host = hostPart;
        *port = 80;
    }
}

// 读访问日志，找 first 之后是否存在 method+path 行。返回行号或 -1。
int findLine(const QStringList& lines, const QString& method,
             const QString& path, int from)
{
    for (int i = from; i < lines.count(); ++i) {
        if (lines[i] == method + QLatin1Char(' ') + path) {
            return i;
        }
    }
    return -1;
}

class FakeLocal : public DavLocalSource {
public:
    QString base;
    QString relBase() const
    {
        QString b = base;
        if (!b.endsWith(QLatin1Char('/'))) {
            b += QLatin1Char('/');
        }
        return b;
    }
    QString relOf(const QString& abs) const
    {
        const QString b = relBase();
        if (abs.startsWith(b)) {
            return abs.mid(b.length());
        }
        return abs;
    }
    std::vector<DavLocalPack> installedPacksByTitleAsc()
    {
        std::vector<DavLocalPack> v;
        DavLocalPack p;
        p.id = QStringLiteral("p1");
        p.title = QStringLiteral("pastes");
        v.push_back(p);
        return v;
    }
    std::vector<DavLocalSticker> stickersOfPack(const QString&)
    {
        std::vector<DavLocalSticker> v;
        DavLocalSticker a;
        a.filePath = relBase() + QStringLiteral("pastes/regular.png");
        v.push_back(a);
        DavLocalSticker b;
        b.filePath = relBase() + QStringLiteral("pastes/_stikmeta.svg");
        v.push_back(b);
        return v;
    }
    bool isBuiltinSourcePack(const QString&, const QString&) { return false; }
    QString builtinSourceDiag(const QString&, const QString&) { return QString(); }
    QStringList builtinSourceCloudDirs() { return QStringList(); }
    QString relativeToBase(const QString& abs) { return relOf(abs); }
    QString resolveStickerPath(const QString& stored)
    {
        if (stored.startsWith(QLatin1Char('/'))) {
            return stored;
        }
        return relBase() + stored;
    }
    QString ensurePack(const QString&) { return QStringLiteral("p1"); }
    bool importStickerFile(const QString&, const QString&, QString*,
                           const QString&, const QString&) { return true; }
};

} // namespace

void ProbeRecv::onLog(int level, const QString& tag, const QString& line)
{
    std::fprintf(stderr, "[log %d] %s: %s\n", level,
                 qToStdString(tag).c_str(), qToStdString(line).c_str());
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("anystik"));
    QCoreApplication::setApplicationName(QStringLiteral("davsync_e2e"));

    // 与 qlstik/src/main.cpp 同款：curl 泵线程必须先起，否则 WebDAV 动词
    // （OPTIONS/PROPFIND/PUT…）一律报 "transport not ready"。
    EventPoller::start();
    QWebdavTransport::start();
    if (!QWebdavTransport::isReady()) {
        std::fprintf(stderr, "[warn] QWebdavTransport not ready\n");
    }

    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <baseUrl>\n", argv[0]);
        return 2;
    }
    const QString baseUrl = QString::fromUtf8(argv[1]);
    QString host;
    int port = 0;
    parseBase(baseUrl, &host, &port);

    const QString localBase = envStr("DAVSYNC_LOCAL_BASE",
                                     QStringLiteral("/tmp/opencode/e2e-local"));
    const QString logPath = envStr("DAVSYNC_LOG",
                                   QStringLiteral("/tmp/opencode/davsync_access.log"));

    FakeLocal src;
    src.base = localBase;
    setDavLocalSource(&src);

    SyncEngine e;
    const bool connOk = e.setConnectionSettings(
        /*connectionType*/ 1, host, QStringLiteral("/"),
        QStringLiteral("user"), QStringLiteral("pass"), port, /*ssl*/ false);
    check(connOk, "setConnectionSettings");

    ProbeRecv recv;
    QObject::connect(&e, SIGNAL(finished(int, const QString&)),
                     &recv, SLOT(onFinished(int, const QString&)));
    QObject::connect(&e, SIGNAL(progressUpdated(int, const QString&, const QString&)),
                     &recv, SLOT(onProgress(int, const QString&, const QString&)));
    QObject::connect(&e, SIGNAL(treeScanned(int, int)),
                     &recv, SLOT(onTree(int, int)));
    QObject::connect(&e, SIGNAL(syncLog(int, const QString&, const QString&)),
                     &recv, SLOT(onLog(int, const QString&, const QString&)));

    check(e.startSync(), "startSync");

    // 事件泵：最多 60s。Qt3 的 processEvents(ms) 在事件队列空时立即返回
    // （不会睡满 ms），故必须自己 usleep，否则循环空转、异步网络永远收不到。
    const int deadline = 60000;
    int waited = 0;
    while (!recv.finished && waited < deadline) {
        qApp->processEvents(50);
        usleep(10 * 1000);
        waited += 10;
    }

    check(recv.finished, "finished emitted");
    check(recv.exitCode == 0, "exitCode == 0 (FinishOk)");
    check(recv.finCount == 1, "finished exactly once");
    check(recv.treeFiles >= 0, "treeScanned emitted");
    check(recv.prog100 > 0, "progress reached 100");

    // ── 访问日志断言 ──
    QStringList lines;
    {
        QFile f(logPath);
        if (f.open(kReadOnly)) {
            QTextStream ts(&f);
            while (!ts.atEnd()) {
                const QString ln = qTrimmed(ts.readLine());
                if (!ln.isEmpty()) {
                    lines.append(ln);
                }
            }
        }
    }
    check(lines.count() > 0, "access log non-empty");

    const int opt = findLine(lines, QStringLiteral("OPTIONS"),
                             QStringLiteral("/"), 0);
    const int propfind = findLine(lines, QStringLiteral("PROPFIND"),
                                  QStringLiteral("/anystik/"), 0);
    check(opt >= 0, "OPTIONS seen");
    check(propfind >= 0, "PROPFIND /anystik/ seen");
    check(opt >= 0 && propfind >= 0 && opt < propfind,
          "OPTIONS before PROPFIND");

    const int putRegular = findLine(lines, QStringLiteral("PUT"),
                                    QStringLiteral("/anystik/pastes/regular.png"), 0);
    check(putRegular >= 0, "PUT regular.png (upload)");

    const int getMeta = findLine(lines, QStringLiteral("GET"),
                                 QStringLiteral("/anystik/pastes/_stikmeta.svg"), 0);
    const int putMeta = findLine(lines, QStringLiteral("PUT"),
                                 QStringLiteral("/anystik/pastes/_stikmeta.svg"), 0);
    check(getMeta >= 0, "GET _stikmeta.svg (merge pull)");
    check(putMeta >= 0, "PUT _stikmeta.svg (merge push)");
    check(getMeta >= 0 && putMeta >= 0 && getMeta < putMeta,
          "GET before PUT for mergeable");

    int moves = 0;
    int conflicts = 0;
    for (int i = 0; i < lines.count(); ++i) {
        if (lines[i].startsWith(QStringLiteral("MOVE"))) {
            ++moves;
        }
        if (lines[i].contains(QStringLiteral(".conflict"))) {
            ++conflicts;
        }
    }
    check(moves == 0, "no MOVE");
    check(conflicts == 0, "no .conflict path");

    // ── 合并结果：云端 = 本地∪云端 ──
    // 依赖探针运行前已把「云端」_stikmeta.svg 预置到 server root（含 rel=cloud-only），
    // 本地 _stikmeta.svg 含 rel=local-only；合并后二者皆在。
    QString mergedCloud;
    {
        QFile f(QStringLiteral("/tmp/opencode/e2e-srv/anystik/pastes/_stikmeta.svg"));
        if (f.open(kReadOnly)) {
            QTextStream ts(&f);
            mergedCloud = ts.read();
        }
    }
    check(mergedCloud.contains(QStringLiteral("local-only")),
          "merged cloud has local-only");
    check(mergedCloud.contains(QStringLiteral("cloud-only")),
          "merged cloud has cloud-only");

    std::fprintf(stderr, "E2E-DONE: fails=%d\n", g_fails);
    QWebdavTransport::stop();
    EventPoller::stop();
    std::printf("E2E-%s: fails=%d\n", g_fails == 0 ? "OK" : "FAIL", g_fails);
    return g_fails == 0 ? 0 : 1;
}
