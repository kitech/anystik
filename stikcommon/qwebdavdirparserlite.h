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

// 跨版本头：本 .cpp 自 2026-09-30 起**真正进入 qlstik 应用构建**
// （此前只登记了头、.cpp 漏挂，见 .pri 批次 3c-1 第五阶段段末的补记），
// 故 Qt3 与 Qt6 都要编。两版头文件名不同（小写 vs 驼峰），不能混用。
#ifdef QT3_BUILD
#include <qobject.h>
#include <qstring.h>
#include <qdatetime.h>
#else
#include <QObject>
#include <QString>
#include <QDateTime>
#endif
#include <functional>
#include <vector>
#include "qglobaltype_shim.h"

class QWebdavLite;
class QNetworkReply;
class QWebdavItemLite;

// ── 为何现在有 QObject 基类（2026-09-30 补）────────────────────────────
// 本类原先**不是 QObject 子类**，只因为 3c-1 阶段唯一的使用者是 /tmp 下的
// 独立探针，探针是栈上分配 + 手工 push std::function，故「非 QObject + 槽表」
// 够用。但它唯一的真实消费者 davbisync 把它当 QObject 用，实测 4 处全挂：
//   davbisync.cpp:116  p->deleteLater()   → has no member named 'deleteLater'
//   davbisync.cpp:115  disconnect(p,nullptr,this,nullptr) → 无匹配重载
//   davbisync.cpp:120  connect(p,&QObject::destroyed,...) → 无匹配重载
//   davbisync.cpp:122  connect(p,...,finished,...) → 无匹配重载（无 PMF 模板）
// 且构造函数原本 `Q_UNUSED(parent)` 把 parent 直接丢弃，没有父子接管。
// 加上 QObject 基类即可一次解决前 3 类 + parent 接管，**且不需要 Q_OBJECT**：
// QObject 基类本身就提供 deleteLater / disconnect / &QObject::destroyed /
// 父子所有权，这四项都不依赖本类自己的元对象。
//
// 为何仍保留 std::function 槽表（而非纯 moc 信号）：3c-1 的 207 e2e 探针
// （/tmp/opencode/dav207e.cpp:58-63）就是栈上分配本类 + 手工 push 槽，那个绿门
// 不能被这次改造撞掉。故 emit* 是**唯一发射点**，同时打两条路：
//   1. 遍历 slots_*（Qt3 消费路径 + 207 探针）
//   2. emit 原生信号（Qt4+ 消费路径，davbisync 走这条）
// 两条路内容一致，不存在「只有一端收到」的分叉。
class QWebdavDirParserLite : public QObject
{
    Q_OBJECT
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
    // 原生信号（Qt4+ 走这里；moc 由 qlstik.pro 的 CONFIG += moc 生成，
    // 头已在 stikcommon.pri:143 的 STIKCOMMON_HEADERS 里登记）。
    // ⚠ 信号名不能与下面的 emit* 方法同名，故用 finished / errorChanged，
    //   发射统一走 emit*（那里同时打原生信号与槽表）。
signals:
    void finished();
    void errorChanged(const QString& e);

public:
    // ⚠ 上面 signals: 段必须**只**放两条信号声明，紧跟一个 public: 重新打开。
    //   漏掉这个 public: 会让下面这些 typedef / slots_* / emit* 全被归进
    //   signals 段，Qt 3.5 的 moc 会把它们当信号解析，对
    //   `std::vector<FinishedSlot>& slots_finished()` 报
    //     Warning: Unexpected variable declaration.
    //   随后 moc_qwebdavdirparserlite.cpp 编译期炸：
    //     error: 'FinishedSlot' was not declared in this scope
    //     error: no declaration matches 'int& QWebdavDirParserLite::slots_finished()'
    //   （"int&" 是 moc 把返回类型也猜错了。已实测，非推断。）

    // finished 无参：davbisync.cpp:122 的 lambda 就是无参。
    typedef std::function<void()> FinishedSlot;
    std::vector<FinishedSlot>& slots_finished() { return m_slotsFinished; }
    void emitFinished()
    {
        for (size_t i = 0; i < m_slotsFinished.size(); ++i) {
            m_slotsFinished[i]();
        }
        emit finished();
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
        emit errorChanged(e);
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

// ── Qt3/Qt4+ 的分工（范式对齐 qwebdavlite.h:191-200 的既有约定）────────
//   Qt3   —— 无 PMF connect，走下面的 slots_* 槽表（已验证的 207 探针即此路）。
//   Qt4+  —— 走原生 QObject::connect(成员函数指针)，靠上面 Q_OBJECT + moc。
// ⚠ 不要在 Qt4+ 下再定义全局 connect 模板：那会与原生重载产生歧义
//   （实测报 "static assertion failed: No Q_OBJECT in the class with the
//   signal"，因为 QObject::connect 作为**成员函数**先于 ADL 被查到）。
//   这条约束在 qwebdavlite.h:191-200 已写明，此处不重复实现模板。
#endif
