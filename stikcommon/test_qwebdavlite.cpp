// stikcommon/test_qwebdavlite.cpp —— QWebdavLite 纯方法契约（Qt3 单端，零网络）
//
// 只测**不发起任何请求**的部分：
//   * 连接配置的存/取（setConnectionSettings + getter）
//   * absolutePath：rootPath 与 relPath 的拼接规则
//   * digestToHex / digestFromHex：证书摘要的 "XX:XX:…" 大写 hex 互转
//   * buildUrl / mkcolRequest / proFindRequest：URL 拼装与 DAV 请求头
//   * errorChanged 的垫片订阅链（纯内存）
//
// 刻意**不测** get/put/mkdir/move/remove/options/head/propfind：
// 它们一律经 createRequest → QNAM::issue → EventPoller → curl_multi 真的发包，
// 属于 e2e，见 test_dav207.cpp 的 fixture 路线。构造函数本身安全（只挂认证槽）。
//
// 受保护方法经 Probe 子类暴露，避免为测试放宽生产类的可见性。
//
// ★ Qt3 QString + doctest 2.4.11 的一个必须绕开的雷
//
//   本 Qt 3.3.8 的 QString 区分 null 与 empty：
//     QString()    isNull()==1，长度 0
//     QString("")  isNull()==0，长度 0
//   两者 `==` 为 **false**（已实测）。更糟的是：doctest 2.4.11 在**格式化 null
//   QString** 时会 SIGSEGV（已实测：CHECK(QString() == QString("")) 直接
//   "test case CRASHED: SIGSEGV"，assertions 计数为 0 —— 崩在失败消息构造阶段，
//   还没算上这条断言）。非 null 的 QString 打印是正常的。
//   故本文件凡可能拿到 null 的字符串，一律断言 .isEmpty() / .length() 这类
//   **返回 bool/int 的表达式**，不把 QString 送进 == 的比较链。

#include <cstdio>
#include <string>

#include "doctest/doctest.h"

#include "qwebdavlite.h"

// 把受保护成员开放给测试用
class Probe : public QWebdavLite {
public:
    Probe() : QWebdavLite(0) {}
    QString url(const QString& p) { return buildUrl(p); }
    QNetworkRequest mkcol(const QString& p) { return mkcolRequest(p); }
    QNetworkRequest proFind(const QString& p, int depth = 0)
    {
        return proFindRequest(p, depth);
    }
};

// 直接读底层 map，绕开 Qt3 QByteArray 的比较麻烦（值是 std::string）
static std::string hdr(const QNetworkRequest& r, const std::string& k)
{
    const std::map<std::string, std::string>& m = r.rawHeaders();
    std::map<std::string, std::string>::const_iterator it = m.find(k);
    return (it == m.end()) ? std::string("<absent>") : it->second;
}

// Qt3 的 QByteArray = QMemArray<char>，没有 (const char*, int) 构造（那是受保护的
// 内部构造），只能按长度申请后逐字节填。
static QByteArray bytes(const unsigned char* p, int n)
{
    QByteArray b(n);
    for (int i = 0; i < n; ++i) {
        b.at(i) = (char)p[i];
    }
    return b;
}

static bool hasHdr(const QNetworkRequest& r, const std::string& k)
{
    return r.rawHeaders().find(k) != r.rawHeaders().end();
}

// ── 构造默认值 ─────────────────────────────────────────────────────

TEST_CASE("QWebdavLite: 构造后的默认状态")
{
    Probe w;
    CHECK(w.connectionType() == QWebdavLite::HTTP);
    CHECK(w.isSSL() == false);
    // ⚠ 这三个是默认构造的 **null** QString（不是 empty）：见文件头关于
    //   doctest 格式化 null QString 会 SIGSEGV 的说明，故只断言 isEmpty()/长度。
    CHECK(w.hostname().isEmpty());
    CHECK(w.hostname().length() == 0);
    CHECK(w.port() == 0);
    CHECK(w.rootPath() == QString("/"));      // m_rootPath 由 ctor 初始化为 "/"
    CHECK(w.username().isEmpty());
    CHECK(w.password().isEmpty());
    // 空闲超时默认关闭（<0）
    CHECK(w.transferTimeout() == -1);
}

// ── 连接配置 ───────────────────────────────────────────────────────

TEST_CASE("QWebdavLite: setConnectionSettings 写入并可读回")
{
    Probe w;
    w.setConnectionSettings(QWebdavLite::HTTPS, QString("dav.example.com"),
                            QString("/remote.php/dav"),
                            QString("alice"), QString("s3cr3t"), 8443);

    CHECK(w.connectionType() == QWebdavLite::HTTPS);
    CHECK(w.isSSL() == true);
    CHECK(w.hostname() == QString("dav.example.com"));
    CHECK(w.port() == 8443);
    CHECK(w.rootPath() == QString("/remote.php/dav"));
    CHECK(w.username() == QString("alice"));
    CHECK(w.password() == QString("s3cr3t"));

    // 切回 HTTP，isSSL 跟着变
    w.setConnectionSettings(QWebdavLite::HTTP, QString("h"), QString("/"),
                            QString("u"), QString("p"), 80);
    CHECK(w.isSSL() == false);
    CHECK(w.connectionType() == QWebdavLite::HTTP);
    CHECK(w.port() == 80);
}

TEST_CASE("QWebdavLite: setConnectionSettings 的默认参数只覆盖显式项")
{
    Probe w;
    // 只给类型和主机，其余走默认值
    w.setConnectionSettings(QWebdavLite::HTTP, QString("only.host"));
    CHECK(w.hostname() == QString("only.host"));
    CHECK(w.rootPath() == QString("/"));
    // 未显式给的凭据仍是 null QString → 用 isEmpty() 断言
    CHECK(w.username().isEmpty());
    CHECK(w.password().isEmpty());
    CHECK(w.port() == 0);          // 0 → URL 不带端口
    CHECK(w.isSSL() == false);
}

TEST_CASE("QWebdavLite: 空闲超时读写")
{
    Probe w;
    w.setTransferTimeout(30000);
    CHECK(w.transferTimeout() == 30000);
    w.setTransferTimeout(-1);      // 关闭
    CHECK(w.transferTimeout() == -1);
    w.setTransferTimeout(0);
    CHECK(w.transferTimeout() == 0);
}

// ── rootPath 归一 ──────────────────────────────────────────────────

TEST_CASE("QWebdavLite: setConnectionSettings 归一 rootPath")
{
    Probe w;
    // 规则（setConnectionSettings 末尾）：非空 → 以 '/' 开头 → 不以 '/' 结尾
    //   （长度 > 1 才去尾，故 "/" 自身保留）
    w.setConnectionSettings(QWebdavLite::HTTP, QString("h"), QString(""));
    CHECK(w.rootPath() == QString("/"));            // 空 → "/"

    w.setConnectionSettings(QWebdavLite::HTTP, QString("h"), QString("dav"));
    CHECK(w.rootPath() == QString("/dav"));         // 补首 "/"

    w.setConnectionSettings(QWebdavLite::HTTP, QString("h"), QString("/dav/"));
    CHECK(w.rootPath() == QString("/dav"));         // 去尾 "/"

    w.setConnectionSettings(QWebdavLite::HTTP, QString("h"), QString("/"));
    CHECK(w.rootPath() == QString("/"));            // 长度 1，不去尾

    w.setConnectionSettings(QWebdavLite::HTTP, QString("h"), QString("/a/b/"));
    CHECK(w.rootPath() == QString("/a/b"));

    w.setConnectionSettings(QWebdavLite::HTTP, QString("h"), QString("//"));
    CHECK(w.rootPath() == QString("/"));            // 长度 2 → 去一个尾 "/"

    w.setConnectionSettings(QWebdavLite::HTTP, QString("h"), QString("a/b"));
    CHECK(w.rootPath() == QString("/a/b"));         // 只补首，不动中间的 "/"
}

// ── absolutePath ───────────────────────────────────────────────────

TEST_CASE("QWebdavLite: absolutePath 的 rootPath/relPath 拼接规则")
{
    Probe w;
    // 注意 rootPath 已被 setConnectionSettings 归一（去尾 "/"），故这里
    // "以 / 结尾" 的分支只有 rootPath == "/" 能走到。
    // relPath 空 → 原样返回 rootPath
    w.setConnectionSettings(QWebdavLite::HTTP, QString("h"), QString("/dav"));
    CHECK(w.absolutePath(QString("")) == QString("/dav"));

    // rootPath 已是 "/" 前缀，不重复加
    w.setConnectionSettings(QWebdavLite::HTTP, QString("h"), QString("/"));
    CHECK(w.absolutePath(QString("foo")) == QString("/foo"));
    // relPath 自带前导 "/" → 剥掉再拼，避免出现 "//"
    CHECK(w.absolutePath(QString("/foo")) == QString("/foo"));
    CHECK(w.absolutePath(QString("/a/b")) == QString("/a/b"));
    // 嵌套路径原样保留
    CHECK(w.absolutePath(QString("a/b/c.txt")) == QString("/a/b/c.txt"));

    // rootPath 无尾斜杠 → 自动补一个再拼
    w.setConnectionSettings(QWebdavLite::HTTP, QString("h"), QString("/dav"));
    CHECK(w.absolutePath(QString("foo")) == QString("/dav/foo"));
    CHECK(w.absolutePath(QString("/foo")) == QString("/dav/foo"));
    CHECK(w.absolutePath(QString("a/b")) == QString("/dav/a/b"));

    // ⚠ rootPath="/dav/" 传入后已被归一成 "/dav"，故 absolutePath("") 是 "/dav"
    //   而不是 "/dav/" —— 尾斜杠在建配置时就已吃掉。
    w.setConnectionSettings(QWebdavLite::HTTP, QString("h"), QString("/dav/"));
    CHECK(w.rootPath() == QString("/dav"));
    CHECK(w.absolutePath(QString("")) == QString("/dav"));
    CHECK(w.absolutePath(QString("foo")) == QString("/dav/foo"));

    // rootPath == "/" + relPath 空：返回 "/"，不是 ""
    w.setConnectionSettings(QWebdavLite::HTTP, QString("h"), QString("/"));
    CHECK(w.absolutePath(QString("")) == QString("/"));
}

// ── 证书摘要 hex ───────────────────────────────────────────────────

TEST_CASE("QWebdavLite: digestToHex 输出大写 XX:XX 形式")
{
    static const unsigned char kThree[3] = { 0x00, 0x0f, 0xa5 };
    const QByteArray in = bytes(kThree, 3);
    const QString hex = QWebdavLite::digestToHex(in);
    CHECK(hex == QString("00:0F:A5"));
    CHECK(hex.length() == 8);          // 3 字节 → 2*3 个字符 + 2 个冒号

    // 单字节 0xFF
    static const unsigned char kOne[1] = { 0xff };
    CHECK(QWebdavLite::digestToHex(bytes(kOne, 1)) == QString("FF"));
    // 全零
    static const unsigned char kTwoZero[2] = { 0x00, 0x00 };
    CHECK(QWebdavLite::digestToHex(bytes(kTwoZero, 2)) == QString("00:00"));
    // 空输入 → 空串（提前返回，不产生 ":"）
    CHECK(QWebdavLite::digestToHex(QByteArray()).isEmpty());
}

TEST_CASE("QWebdavLite: digestFromHex 去冒号并解回字节")
{
    const QByteArray back = QWebdavLite::digestFromHex(QString("00:0F:A5"));
    CHECK(back.size() == 3);
    CHECK((unsigned char)back.at(0) == 0x00);
    CHECK((unsigned char)back.at(1) == 0x0F);
    CHECK((unsigned char)back.at(2) == 0xA5);

    // 无冒号也吃
    const QByteArray plain = QWebdavLite::digestFromHex(QString("00FF"));
    CHECK(plain.size() == 2);
    CHECK((unsigned char)plain.at(0) == 0x00);
    CHECK((unsigned char)plain.at(1) == 0xFF);

    // 小写 hex 同样能解（qbaFromHex 不区分大小写）
    const QByteArray lower = QWebdavLite::digestFromHex(QString("ab:cd"));
    CHECK(lower.size() == 2);
    CHECK((unsigned char)lower.at(0) == 0xAB);
    CHECK((unsigned char)lower.at(1) == 0xCD);

    // 空串
    CHECK(QWebdavLite::digestFromHex(QString("")).size() == 0);
}

TEST_CASE("QWebdavLite: digestToHex/digestFromHex 往返一致")
{
    // 16 字节，模拟 MD5/SHA1 长度
    QByteArray raw(16);
    for (int i = 0; i < 16; ++i) {
        raw.at(i) = (char)(i * 17);       // 0,17,34,... 覆盖高/低位
    }
    const QString hex = QWebdavLite::digestToHex(raw);
    CHECK(hex.length() == 16 * 3 - 1);    // 16*2 字符 + 15 个冒号
    const QByteArray back = QWebdavLite::digestFromHex(hex);
    CHECK(back.size() == raw.size());
    bool same = (back.size() == raw.size());
    for (int i = 0; same && i < raw.size(); ++i) {
        same = ((unsigned char)back.at(i) == (unsigned char)raw.at(i));
    }
    CHECK(same);
}

// ── buildUrl ───────────────────────────────────────────────────────

TEST_CASE("QWebdavLite: buildUrl 拼 scheme/host/port/path")
{
    Probe w;
    w.setConnectionSettings(QWebdavLite::HTTP, QString("example.com"),
                            QString("/dav"), QString(), QString(), 0);
    const QUrl u(QUrl(w.url(QString("a.txt"))));
    CHECK(u.protocol() == QString("http"));
    CHECK(u.host() == QString("example.com"));
    CHECK(u.path() == QString("/dav/a.txt"));
    // port 0 → setPort 不被调用，Qt3 QUrl::port() 对"无端口"返回 -1（不是 0）
    CHECK(u.port() == -1);
    // 凭据绝不能进 userinfo（buildUrl 注释里记了 libcurl 静默丢请求的坑）
    // 无 userinfo 时 Qt3 的 user() 返回 **null** QString → 只能断言 isEmpty()
    CHECK(u.user().isEmpty());
    CHECK(u.toString().find("@") == -1);
}

TEST_CASE("QWebdavLite: buildUrl 在 HTTPS + 显式端口下正确")
{
    Probe w;
    w.setConnectionSettings(QWebdavLite::HTTPS, QString("dav.example.com"),
                            QString("/"), QString("alice"), QString("pw"), 8443);
    const QUrl u(QUrl(w.url(QString(""))));
    CHECK(u.protocol() == QString("https"));
    CHECK(u.host() == QString("dav.example.com"));
    CHECK(u.port() == 8443);
    CHECK(u.path() == QString("/"));
    // 用户名/密码已设置，但不得出现在 URL 里
    CHECK(w.username() == QString("alice"));
    CHECK(u.user().isEmpty());
    CHECK(w.url(QString("")).find("alice") == -1);
    CHECK(w.url(QString("")).find("pw") == -1);
}

TEST_CASE("QWebdavLite: buildUrl 的 path 走 absolutePath")
{
    Probe w;
    w.setConnectionSettings(QWebdavLite::HTTP, QString("h"), QString("/base"));
    CHECK(QUrl(w.url(QString("x/y"))).path() == QString("/base/x/y"));
    CHECK(QUrl(w.url(QString("/x/y"))).path() == QString("/base/x/y"));
    // 端口为 0 时不出现 "host:0"
    CHECK(w.url(QString("")).find(":0") == -1);
}

// ── DAV 请求头 ─────────────────────────────────────────────────────

TEST_CASE("QWebdavLite: mkcolRequest 带 Connection/User-Agent")
{
    Probe w;
    w.setConnectionSettings(QWebdavLite::HTTP, QString("h"), QString("/dav"));
    const QNetworkRequest r = w.mkcol(QString("newdir"));
    CHECK(hdr(r, "Connection") == std::string("close"));
    CHECK(hdr(r, "User-Agent") == std::string("qlstik"));
    // MKCOL 不该带 PROPFIND 专属头
    CHECK(hasHdr(r, "Depth") == false);
    CHECK(hasHdr(r, "Content-Type") == false);
    // Qt3 的 QNetworkRequest::url() 返回 QString（Qt4+ 才是 QUrl）
    CHECK(QUrl(r.url()).path() == QString("/dav/newdir"));
}

TEST_CASE("QWebdavLite: proFindRequest 的 Depth 只区分 0 与非 0")
{
    Probe w;
    w.setConnectionSettings(QWebdavLite::HTTP, QString("h"), QString("/dav"));

    const QNetworkRequest d0 = w.proFind(QString("p"), 0);
    CHECK(hdr(d0, "Depth") == std::string("0"));
    CHECK(hdr(d0, "Content-Type") == std::string("text/xml; charset=utf-8"));
    CHECK(hdr(d0, "Connection") == std::string("close"));

    const QNetworkRequest d1 = w.proFind(QString("p"), 1);
    CHECK(hdr(d1, "Depth") == std::string("1"));

    // ⚠ 深度被钳到 1：WebDAV 只允许 0/1，depth=2、99 都发 "1"
    //   （proFindRequest 写的是 depth > 0 ? "1" : "0"）
    CHECK(hdr(w.proFind(QString("p"), 2), "Depth") == std::string("1"));
    CHECK(hdr(w.proFind(QString("p"), 99), "Depth") == std::string("1"));
    // 负数也当 0
    CHECK(hdr(w.proFind(QString("p"), -1), "Depth") == std::string("0"));

    // 默认参数 depth=0
    const QNetworkRequest dDef = w.proFind(QString("p"));
    CHECK(hdr(dDef, "Depth") == std::string("0"));
    // MKCOL 与 PROPFIND 的头集合不串
    CHECK(hasHdr(w.mkcol(QString("p")), "Depth") == false);
}

// ── errorChanged 垫片订阅链 ────────────────────────────────────────

TEST_CASE("QWebdavLite: errorChanged 订阅链按序触发")
{
    Probe w;
    std::vector<std::string> seen;
    // Qt3 的 QString 无 toString()，utf8() 返回 QCString（NUL 结尾，取 .data()
    // 即得原字节；本套件断言串全为 ASCII，不涉及编码转换）
    w.subscribeErrorChanged([&seen](const QString& e) {
        seen.push_back(std::string(e.utf8().data()));
    });
    w.subscribeErrorChanged([&seen](const QString& e) {
        seen.push_back(std::string("second:") + std::string(e.utf8().data()));
    });
    CHECK(w.slots_errorChanged().size() == 2);

    w.emitErrorChanged(QString("401 unauthorized"));
    CHECK(seen.size() == 2);
    CHECK(seen[0] == "401 unauthorized");
    CHECK(seen[1] == "second:401 unauthorized");

    w.emitErrorChanged(QString("ssl: bad cert"));
    CHECK(seen.size() == 4);
    CHECK(seen[2] == "ssl: bad cert");
    CHECK(seen[3] == "second:ssl: bad cert");
}
