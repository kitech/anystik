// QWebdavDirParserLite —— 207 多状态 PROPFIND 响应的窄切面解析器。
//
// vendor 原件：anystik/vendor/qwebdav/qwebdavdirparser.{h,cpp}（119 + 545 行），
// 依赖 vendor QWebdav*、QRecursiveMutex、QNaturalSort、9 个扩展属性。
// davbisync 实测只用 6 个 public 成员 + 4 个条目字段（穷尽确认见
// davbisync.cpp:112-131/239/371/382/384/388/394/395），故按窄切面重写。
//
// ── 与 vendor 的差异，以及每条的依据 ───────────────────────────────
// 1. 第一入参 QWebdav* → QWebdavLite*：vendor 类整体不移植。
// 2. **丢弃 m_mutex**（vendor h:109 QRecursiveMutex）。它在 cpp 里只被
//    replyFinished() 一处用（vendor cpp:204），replyDeleteLater 里那把锁是
//    被注释掉的（cpp:260）——整个类实质单线程无锁。Qt3 亦无 QRecursiveMutex。
// 3. **丢弃 m_includeRequestedURI**（vendor h:114）：davbisync 恒传 false。
// 4. **丢弃 listItem/getDirectoryInfo/getFileInfo/isBusy/isFinished/path**
//    （vendor h:76-86）：davbisync 零调用。
// 5. **丢弃 8 个扩展属性 + href 百分号解码 + 4 级日期兜底**：
//    只解析 davbisync 真正读的 3 个 —— getcontentlength → size、
//    getlastmodified → lastModified、resourcetype → isDir。
//    （日期保留 RFC1123 + ISO 两条，vendor 的非标准 dt 属性与
//      "05 Jan 2024 10:20:30" 兜底是给畸形服务器留的，davbisync 不依赖。）
// 6. **丢弃 replyDeleteLater 的自旋排空**（vendor cpp:249-257）：改 deleteLater。
// 7. 无 Q_OBJECT / 无 moc：信号走 std::function 槽表，范式同
//    qwebdavlite.h:121-129 的 errorChanged。
//
// ── 顺手修掉的 vendor 真 bug（窄切面不继承）────────────────────────
// A. **不校验 207 状态码**（vendor cpp:224 只判 contentType.contains("xml")）。
//    服务端回 207 但 Content-Type 异常 → 不解析、不报错，cpp:266 照发
//    finished() → davbisync 收到**空列表** → 判定「云端 0 文件」→ **全量下载**。
//    这是最危险的静默失败链。本实现显式校验 status code == 207，非 207 走
//    errorChanged，绝不静默返回空列表。
// B. **m_dirList.last() 未判空**（vendor cpp:316）。parseResponse 在 href 为
//    null 时 return 而不 append（cpp:341-342），此时 last() 可能空表 UB 或
//    指向**上一条** item → 误删别人的条目。
// C. **path_.remove(0, rootPath.size()) 不校验前缀**（vendor cpp:486）。
//    不匹配时会砍掉任意长度的前导字符。
// D. **m_abort 未复位**（vendor cpp:105-132 listItem 漏了 m_abort=false）。
// E. propstat 只特判 404（vendor cpp:399），403/401 等其他非 2xx 会把该
//    propstat 的值并进条目。本实现按「非 2xx 即整段跳过」处理。
#ifndef QWEBDAVDIRPARSERLITE_H
#define QWEBDAVDIRPARSERLITE_H

#include <qobject.h>
#include <qstring.h>
#include <qdatetime.h>
#include <functional>
#include <vector>
#include "qglobaltype_shim.h"

class QWebdavLite;
class QNetworkReply;
class QWebdavItemLite;

class QWebdavDirParserLite
{
public:
    explicit QWebdavDirParserLite(QObject* parent = 0);
    ~QWebdavDirParserLite();

    // ── davbisync 的 4 个真实调用点 ──
    // 同步返回 bool：立即拒绝（busy / webdav 空 / path 空 / 无尾斜杠）时 false，
    // davbisync 收到后立刻 finishWithError（davbisync.cpp:371-374）。
    bool listDirectory(QWebdavLite* pWebdav, const QString& path,
                       bool recursive = false);
    // 返回按值拷贝（davbisync.cpp:382 即 const auto items = ...getList()）。
    // Qt3 下 QList 是指针宏，qlist_shim.h 补值容器；getList 的返回类型在
    // .cpp 里用 typedef 暴露，头里不写 QList 以免强制所有 include 者都
    // 承担 qlist_shim 的 include 顺序约束。
    void getList(std::vector<QWebdavItemLite>& out) const;
    void abort();

    // ── 垫片信号（非 moc）──
    // finished 无参：davbisync.cpp:122 的 lambda 就是无参。
    typedef std::function<void()> FinishedSlot;
    std::vector<FinishedSlot>& slots_finished() { return m_slotsFinished; }
    void emitFinished()
    {
        for (size_t i = 0; i < m_slotsFinished.size(); ++i) {
            m_slotsFinished[i]();
        }
    }

    // errorChanged 只记日志、不中断（davbisync.cpp:127-130），但**必须真触发**，
    // 否则 bug A 的静默失败会一路伪装成「云端空了」。
    typedef std::function<void(QString)> ErrorSlot;
    std::vector<ErrorSlot>& slots_errorChanged() { return m_slotsErrorChanged; }
    void emitErrorChanged(const QString& e)
    {
        for (size_t i = 0; i < m_slotsErrorChanged.size(); ++i) {
            m_slotsErrorChanged[i](e);
        }
    }

private:
    void onReplyFinished(QNetworkReply* reply);
    // 207 解析已下沉到 stikcommon/dav207pugi.cpp（pugixml 后端，全程
    // std::string，不过 Qt）。本类只做三件事：把 rootPath 传下去、把
    // std::string 用 QString::fromUtf8 转成 QString、跳过容器自身。
    // 原先的 davParsePropstats / codeFromResponse / parseDateTime 已随之移除
    // （状态码解析、RFC1123/850/asctime 日期解析、href 百分号解码全在 dav207 层）。
    void parseMultiResponse(const QByteArray& data);

    QWebdavLite* m_webdav;
    QNetworkReply* m_reply;
    std::vector<QWebdavItemLite> m_dirList;
    QString m_path;
    bool m_busy;
    bool m_abort;

    std::vector<FinishedSlot> m_slotsFinished;
    std::vector<ErrorSlot> m_slotsErrorChanged;
};

#endif