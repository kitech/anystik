// QWebdavDirParserLite 实现。窄切面依据见 qwebdavdirparserlite.h 顶部注释。
#include "qwebdavdirparserlite.h"
#include "qwebdavitemlite.h"
#include "qwebdavlite.h"
// qnam_shim.h **无任何版本守卫**（内部直接 include <qcstring.h> 等 Qt3 小写头），
// 只能在 Qt3 下包含；Qt4+ 的 QNetworkRequest/Reply 由 qwebdavlite.h 的条件包含
// 带进来（qwebdavlite.h:49-56）。守卫形式照抄同族文件，不自创。
#if !defined(QT_VERSION) || QT_VERSION < 0x040000
#include "qnam_shim.h"
#endif
#include "qdatetime_shim.h"

// 207 XML 解析走 dav207pugi.cpp（pugixml 后端），**不用 QDom**。
// 原先用 QDomDocument 的三个理由都已失效或被实测推翻：
//   1. QDom 的 QString 通道在 Qt3 与 Qt5/6 编码语义不同，且 QString::utf8() /
//      latin1() 共用同一静态转换缓冲区 → 同一表达式内先后调用互相覆盖，
//      实测把 /dir/笔记.txt 污染成 /dir/笔记.txt<U+006C>。dav207 层全程
//      std::string，不过 Qt，两版行为一致。
//   2. QDom 的 elementsByTagName/tagName 只比本地名、忽略命名空间（需
//      setContent(s,true) 才拆前缀），第三方命名空间的 collection 会误判成
//      目录；dav207 层用「剥前缀比本地名 + DAV 前缀白名单」并要求直接子元素。
//   3. 原 parseDateTime 的 4 级日期兜底与 vendor 的畸形服务器兼容有关，
//      dav207 层自己解析 RFC1123/RFC850/asctime 三种标准格式，不依赖 Qt。
// dav207 层只回 std::string，进入 Qt 世界只有 **QString::fromUtf8** 一条路
// （Qt3 侧 QString(const char*) 是 Latin-1，绝不能用）。
#include "dav207iface.h"

#include <stdio.h>
#include <algorithm>
// 注：原先此处 include "qlist_shim.h"，实测本文件 QList/QValueList **零引用**
// （容器已全改 std::vector，见 getList 的 std::vector<QWebdavItemLite>& 出参），
// 是 3c-1 改 QDom 时的残留。该头是 Qt3-only（内部 include <qvaluelist.h>），
// 留着会让本 .cpp 无法进 Qt5+ 构建。零引用已用 grep 核实，非推断。

QWebdavDirParserLite::QWebdavDirParserLite(QObject* parent)
    : QObject(parent), m_webdav(0), m_reply(0), m_busy(false), m_abort(false)
{
}

QWebdavDirParserLite::~QWebdavDirParserLite()
{
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = 0;
    }
}

// ── listDirectory ──────────────────────────────────────────────────────
bool QWebdavDirParserLite::listDirectory(QWebdavLite* pWebdav, const QString& path,
                                         bool recursive)
{
    // 与 vendor qwebdavdirparser.cpp:85-101 的拒绝条件对齐（逐条对应）：
    //   :86 m_webdav 空 → :87 已在途 → :88 m_busy
    //   :89 path 空 → :90 m_reply 占着
    // 另加 :87-88 注释所述的尾斜杠校验。davbisync 靠 davbisync.cpp:364-366
    // 手工补尾斜杠来满足它，故保留该校验不会误伤消费者。
    if (pWebdav == 0 || path.isEmpty() || m_reply != 0 || m_busy) {
        return false;
    }
    if (!path.endsWith("/")) {
        return false;
    }

    m_webdav = pWebdav;
    m_path = path;
    m_abort = false;   // 修 vendor bug D：每次新请求必须复位
    // vendor 是「先发后清」（cpp:96 发、:99 清），listItem 却是「先清后发」。
    // 这里统一为**先清后发**：清空早于发请求，出错也不会留着上一次的旧条目。
    m_dirList.clear();

    // 走 QWebdavLite::propfind(path, depth)。⚠ vendor 走的是 QWebdav::list()
    // （vendor cpp:96），而窄切面 QWebdavLite **没有 list()**，只有 propfind()
    // （qwebdavlite.h:116）—— 后者正是前者的底座，Depth 与 body 语义一致。
    // davbisync 恒传 false（davbisync.cpp:371），故实际只会出现 Depth: 1。
    m_reply = m_webdav->propfind(path, recursive ? 2 : 1);
    if (m_reply == 0) {
        m_busy = false;
        return false;
    }
    m_busy = true;

    // 连接 finished。此类**不是 QObject 子类**（窄切面无 moc、只需一个信号出口），
    // 故不能走字符串式或成员函数指针 connect —— 直接用垫片的槽表注册，
    // 范式同 qwebdavtransport / qwebdavlite 对 QNetworkReply::finished 的挂法。
#if !defined(QT_VERSION) || QT_VERSION < 0x040000
    m_reply->slots_finished().push_back(std::function<void()>([this]() {
        onReplyFinished(m_reply);
    }));
#else
    QObject::connect(m_reply, &QNetworkReply::finished, [this]() {
        onReplyFinished(m_reply);
    });
#endif
    return true;
}

void QWebdavDirParserLite::abort()
{
    if (m_abort) {
        return;
    }
    m_abort = true;
    m_busy = false;
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = 0;
    }
    // 注意：**不** emitFinished()。davbisync 的 abort 路径（davbisync.cpp:232-257）
    // 自行 emit 自己的 finished 并置 m_finished，若这里再发会双触发。
    // vendor 在 abort 里同样不发（cpp:173-182）。
}

void QWebdavDirParserLite::getList(std::vector<QWebdavItemLite>& out) const
{
    out = m_dirList;
}

// ── replyFinished ───────────────────────────────────────────────────────
void QWebdavDirParserLite::onReplyFinished(QNetworkReply* reply)
{
    // 防串台：只处理自己登记的那一个（对齐 vendor cpp:192-198）。
    if (reply != m_reply) {
        return;
    }
    if (m_abort) {
        return;
    }

    // 成功判据用 **HTTP status code**，而非 vendor 的
    // contentType.contains("xml")（cpp:224）。见头注释 bug A：那样会让
    // 「207 + 非 xml Content-Type」静默变成空列表 → davbisync 全量下载。
    const int status =
#if !defined(QT_VERSION) || QT_VERSION < 0x040000
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
#else
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
#endif

    if (reply->error() != QNetworkReply::NoError) {
        // ⚠ 走 fromUtf8 而非 QString(const char*)：Qt3 的 QString(const char*)
        // 按 Latin-1 逐字节解释源文件里的 UTF-8 字节，中文会碎成一串 U+00xx
        // 假字符。三处错误文案统一按此处理（含下面两处中文）。
        emitErrorChanged(QString::fromUtf8("webdav dir: %1")
                             .arg(reply->errorString()));
    } else if (status != 207) {
        // 207 Multi-Status 是 PROPFIND 列表的唯一成功状态码。其它 2xx（如某些
        // 服务器对 Depth:1 回 200）按错误处理并把真实码报出去，不静默返回空表。
        emitErrorChanged(QString::fromUtf8("webdav dir: 期望 207 Multi-Status，实收 %1")
                             .arg(status));
    } else {
        const QByteArray data = reply->readAll();
        parseMultiResponse(data);
    }

    if (m_reply) {
        m_reply->deleteLater();
        m_reply = 0;
    }
    m_busy = false;
    emitFinished();   // davbisync 的唯一唤醒点
}

// ── parseMultiResponse ──────────────────────────────────────────────────
void QWebdavDirParserLite::parseMultiResponse(const QByteArray& data)
{
    if (m_abort) {
        return;
    }

    // 交给 dav207 层：pugixml 解析 + 命名空间判定 + href 百分号解码 +
    // 零 2xx 段剔除，全部在 std::string 域内完成。
    // rootPath 传下去由 dav207 层**带前缀校验**地剥离（修 vendor bug C：
    // vendor cpp:486 无条件 remove(0, rootPath.size())，不匹配时砍掉任意
    // 长度的前导字符）。
    //
    // ⚠ Qt3 的 QByteArray 就是 QMemArray<char>，没有 length()，size() 才是
    //   数据长度（实测 QByteArray(9).size()==9 且 data()[8]=='v'），
    //   且 data() **不保证** NUL 终止 → 一律用 (data(), size()) 成对传。
    std::vector<Dav207::Resource> res;
    const QByteArray rootPathBA = qToUtf8BA(m_webdav->rootPath());
    const std::string rootPath(rootPathBA.data(),
                               (size_t)rootPathBA.size());
    if (!Dav207::parseMultiStatus(data.data(), (size_t)data.size(),
                                  rootPath, res)) {
        emitErrorChanged(QString::fromUtf8("webdav dir: 207 响应体非合法 XML"));
        return;
    }

    // 跳过容器自身（vendor cpp:299-307：Apache 回 path 无 authority，
    // IIS 回绝对 URL，两种形态都要比对）。
    const QString selfPath = m_webdav->absolutePath(m_path);
    // dav207 层已剥掉 rootPath，故 selfPath 也要剥同样的前缀才可比。
    // ⚠ 显式 QString::fromUtf8：rootPath 可能含非 ASCII（云目录名可以是中文），
    //   传 const char* 给 startsWith 在 Qt3 下会走 QString(const char*) 的
    //   Latin-1 通道 → 与下面的 selfStripped（fromUtf8 来的）永远比不相等，
    //   结果是容器自身比不掉、被当成一个条目混进列表。
    QString selfStripped = selfPath;
    if (!rootPath.empty()) {
        const QString rootPathQ = QString::fromUtf8(rootPath.c_str(),
                                                    (uint)rootPath.length());
        if (!rootPathQ.isEmpty() && selfStripped.startsWith(rootPathQ)) {
            selfStripped.remove(0, rootPathQ.length());
        }
    }

    for (size_t i = 0; i < res.size(); ++i) {
        if (m_abort) {
            return;
        }
        const Dav207::Resource& r = res[i];
        if (!r.valid) {
            continue;
        }
        // UTF-8 字节 → QString。Qt3 侧 QString(const char*) 是 Latin-1，
        // 只有 fromUtf8 正确（这是本轮踩过的坑，见文件头注释）。
        const QString path = QString::fromUtf8(r.path.c_str(),
                                               (uint)r.path.length());
        if (path.isEmpty()) {
            continue;   // href 缺失/非法：修 vendor bug B 的根因
        }
        if (path == selfStripped) {
            continue;   // 容器自身
        }

        // msec==0 → 无效 QDateTime，davbisync 据此判定「服务器不支持
        // getlastmodified」（davbisync.cpp:396-398）。故只在有值时转换。
        QDateTime lastModified;
        if (r.msec != 0) {
            lastModified = qDateTimeFromMsecs((qint64)r.msec);
        }

        m_dirList.push_back(QWebdavItemLite(path, r.isDir, lastModified,
                                            (quint64)r.size));
    }
}

