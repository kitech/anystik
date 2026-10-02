// stikcommon/test_qclipboard_shim.cpp —— QMimeData/Qt3Clipboard/QGuiApplication 垫片契约（Qt3 单端）
//
// 被测对象：qclipboard_shim.h
//   QMimeData（setData/data/hasFormat/removeFormat/formats/urls/setUrls/
//             setImageData/imageData/clear + QMimeSource 三接口）
//   Qt3Clipboard（mimeData/setMimeData/image/setImage）
//   QGuiApplication::clipboard()
//   qUrlToLocalFile / qUrlFromLocalFile
//
// ── 本文件依赖的环境事实 ──────────────────────────────────────────────────
//
// ⚠【需要 X11】Qt3 的 QClipboard 走 X11 选择机制，没有 DISPLAY 时
//   QApplication 直接 qFatal abort（不是返回空）。故只有 QMimeData 那部分是
//   纯逻辑、任何环境都能跑；Qt3Clipboard 那部分在无 DISPLAY 时**跳过**
//   （见 haveX11()），否则整个测试二进制在无头 CI 里起不来。
//   本机实测可跑：DISPLAY=:0.0。
//
// ⚠【只能有一个 QApplication】test_main.cpp 用的是
//   DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN，整个 run_tests 只有**一个** main。
//   Qt3 不允许多个 QApplication 实例，所以不能用「每个用例 new 一个」的
//   常规 fixture 写法。ensureApp() 用函数内 static 做惰性单例，且**故意
//   不 delete**：QApplication 若在 static 析构期销毁，会和 Qt 自己的静态
//   对象析构顺序打架（exit 时段错误）。
//
// ⚠【Qt3 的 QByteArray::data() 不保证 NUL 结尾】（Qt4+ 才保证）。
//   所以本文件所有字节比较都走 `size()` + 逐字节/或直接 QString 比较，
//   **绝不** printf("%s", ba.data()) —— 那会越界读（本文件开发时真踩到过：
//   10 字节的 "hello-clip" 被打印出 2 个乱码字节，误以为 shim 坏了）。
//
// ⚠【Qt3 的 QByteArray 没有 QByteArray(const char*) 构造】
//   （qcstring.h:101-102 只有 QByteArray() 和 QByteArray(int size)）。
//   `QByteArray("abc")` 会去试 `const char* → int` 转换而编译失败。
//   构造字面量字节一律用 qbaFromRaw(p, len)（qba_shim.h）。

#include "doctest/doctest.h"
#include <stdlib.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qapplication.h>
#include "qclipboard_shim.h"
#include "qba_shim.h"
#include "qimage_shim.h"

// ⚠ 本仓规范：非 ASCII 一律 UTF-8，不用 fromLatin1（AGENTS.md）。
inline QString S(const char* s) { return QString::fromUtf8(s); }

// 惰性单例 QApplication；故意不销毁（见文件头说明）。
static QApplication* ensureApp()
{
    static int    s_argc = 1;
    static char   s_arg0[] = "run_tests";
    static char*  s_argv[2] = { s_arg0, 0 };
    static QApplication* s_app = 0;
    if (!s_app) s_app = new QApplication(s_argc, s_argv);
    return s_app;
}

static bool haveX11()
{
    const char* d = getenv("DISPLAY");
    return (d != 0) && (*d != 0);
}

// 便捷构造字节数组（Qt3 无 QByteArray(const char*)）。
static QByteArray B(const char* s)
{
    return qbaFromRaw(s, (int)strlen(s));
}

// ═══════════════════════════════════════════════════════════════════════
// QMimeData 纯逻辑部分（无需 QApplication / X11）
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("QMimeData: setData/data 往返，hasFormat 反映存在性")
{
    QMimeData md;
    CHECK_FALSE(md.hasFormat(S("text/plain")));
    CHECK(md.data(S("text/plain")).isEmpty());

    md.setData(S("text/plain"), B("hello"));
    CHECK(md.hasFormat(S("text/plain")));
    CHECK(md.data(S("text/plain")) == B("hello"));

    // 覆盖写：后写的赢
    md.setData(S("text/plain"), B("world"));
    CHECK(md.data(S("text/plain")) == B("world"));

    // 未设置的格式返回空而不是崩溃
    CHECK(md.data(S("image/png")).isEmpty());
}

TEST_CASE("QMimeData: 二进制字节不经任何文本转换（含高位字节与内嵌 NUL）")
{
    // 回归点：setData/data 是**原始字节**通道，绝不能被当成文本编解码。
    // 高位字节（0x80-0xFF）和内嵌 NUL 都必须原样保留 ——
    // 剪贴板上放的是 PNG/APNG/GIF 字节流。
    QMimeData md;
    QByteArray raw;
    qBaAppendBytes(raw, "\x89PNG\r\n\x1a\n", 8);
    qBaAppendBytes(raw, "\x00\x01\xFF\xFE", 4);       // 含 NUL 与高位字节

    md.setData(S("image/png"), raw);
    const QByteArray back = md.data(S("image/png"));
    REQUIRE(back.size() == raw.size());
    for (int i = 0; i < (int)raw.size(); ++i)
        CHECK(back.data()[i] == raw.data()[i]);       // 逐字节，不依赖 NUL 结尾
}

TEST_CASE("QMimeData: removeFormat 后 hasFormat 与 formats 同步更新")
{
    QMimeData md;
    md.setData(S("text/plain"), B("a"));
    md.setData(S("image/png"),  B("b"));
    CHECK_EQ(md.formats().count(), 2);

    md.removeFormat(S("image/png"));
    CHECK_FALSE(md.hasFormat(S("image/png")));
    CHECK_EQ(md.formats().count(), 1);
    CHECK(md.formats().contains(S("text/plain")));

    // 移除不存在的格式不应崩溃
    md.removeFormat(S("application/x-never-existed"));
    CHECK_EQ(md.formats().count(), 1);
}

TEST_CASE("QMimeData: clear 清空数据、url、图像")
{
    QMimeData md;
    md.setData(S("text/plain"), B("a"));
    QList<QUrl> urls; urls.append(qUrlFromLocalFile(S("/tmp/x.png")));
    md.setUrls(urls);
    QImage im = qImageNew32(1, 1);
    md.setImageData(im);
    CHECK(md.hasImage());

    md.clear();
    CHECK_EQ(md.formats().count(), 0);
    CHECK(md.data(S("text/plain")).isEmpty());
    CHECK_EQ(md.urls().count(), 0);
    CHECK_FALSE(md.hasImage());
}

// ── QMimeSource 三接口（Qt3 QClipboard::setData 就是靠它们取数据）──────
TEST_CASE("QMimeData: QMimeSource 的 format/provides/encodedData 一致")
{
    // Qt3 的 QClipboard::setData(QMimeSource*) 只认这三个接口，
    // 少一个剪贴板就拿不到数据 —— stickerstore.cpp:2813 的路径依赖于此。
    QMimeData md;
    md.setData(S("text/plain"), B("AAA"));
    md.setData(S("image/png"),  B("BBB"));

    // format(0..) 枚举到 NULL 终止
    const char* f0 = md.format(0);
    const char* f1 = md.format(1);
    REQUIRE(f0 != 0);
    REQUIRE(f1 != 0);
    CHECK(md.format(2) == 0);          // 终止
    CHECK(md.format(-1) == 0);         // 越界不得崩
    CHECK(md.format(999) == 0);

    // 枚举出的名字与 formats() 一致（QMap 按键序，故 text/plain 与 image/png
    // 哪个在前由键序决定，这里只断言「两个都在」）
    const QStringList fs = md.formats();
    CHECK_EQ(fs.count(), 2);
    CHECK(fs.contains(S("text/plain")));
    CHECK(fs.contains(S("image/png")));

    CHECK(md.provides("text/plain"));
    CHECK_FALSE(md.provides("application/x-nope"));

    CHECK(md.encodedData("text/plain") == B("AAA"));
    CHECK(md.encodedData("image/png")  == B("BBB"));
}

TEST_CASE("QMimeData: removeFormat 后 format() 枚举必须同步（缓存失效）")
{
    // 回归点：rebuildKeys() 用 m_keysDirty 做脏缓存。若 removeFormat 忘了
    // 置脏，format() 会继续返回**已删除**的 MIME 名，Qt3 剪贴板于是去
    // encodedData() 问一个已经不在的键 —— 表现为剪贴板内容残留旧格式。
    QMimeData md;
    md.setData(S("text/plain"), B("a"));
    md.setData(S("image/png"),  B("b"));
    REQUIRE(md.format(0) != 0);
    REQUIRE(md.format(1) != 0);

    md.removeFormat(S("image/png"));
    // 枚举里不应再出现 image/png
    for (int i = 0; i < 4; ++i) {
        const char* f = md.format(i);
        if (f) CHECK(S(f) != S("image/png"));
    }
}

TEST_CASE("QMimeData: setImageData 编出合法 PNG 字节并挂 hasImage")
{
    // Qt3 的 PNG 编解码是内建（非插件），实测能编。
    QMimeData md;
    QImage im = qImageNew32(2, 2);
    qImageFillTransparent(im);
    im.setPixel(0, 0, qRgba(0x11, 0x22, 0x33, 0xFF));
    md.setImageData(im);

    CHECK(md.hasImage());
    const QByteArray png = md.data(S("image/png"));
    REQUIRE(png.size() > 8);
    // PNG 签名 89 50 4E 47 0D 0A 1A 0A
    const unsigned char sig[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    for (int i = 0; i < 8; ++i) CHECK((unsigned char)png.data()[i] == sig[i]);
}

TEST_CASE("QMimeData: setImageData 传空图 → 忽略，不清掉已有图像")
{
    QMimeData md;
    QImage im = qImageNew32(1, 1);
    qImageFillTransparent(im);
    md.setImageData(im);
    const QImage before = md.imageData();

    md.setImageData(QImage());          // 空图
    CHECK(md.hasImage());               // 仍保持原图
    CHECK(md.imageData().size() == before.size());
}

// ═══════════════════════════════════════════════════════════════════════
// urls() / setUrls()
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("QMimeData: setUrls/urls 往返 ASCII 路径")
{
    QMimeData md;
    QList<QUrl> urls;
    urls.append(qUrlFromLocalFile(S("/tmp/a.png")));
    urls.append(qUrlFromLocalFile(S("/tmp/b.png")));
    md.setUrls(urls);

    const QList<QUrl> back = md.urls();
    REQUIRE(back.count() == 2);
    CHECK(qUrlToLocalFile(back.at(0)) == S("/tmp/a.png"));
    CHECK(qUrlToLocalFile(back.at(1)) == S("/tmp/b.png"));
}

TEST_CASE("QMimeData: setUrls 非 ASCII 路径必须写成合法 UTF-8（回归）")
{
    // ⚠ 本条是真实 bug 的回归锁。修复前 setUrls() 用
    //     qBaAppendBytes(list, s.latin1(), s.length())
    //   两个错叠加：latin1() 把非 Latin1 码点压成低字节；length() 是
    //   UTF-16 码元数不是字节数。实测 /tmp/贴纸.png 被写成
    //     file:///tmp/??.png      （字节 3F 3F）
    //   本进程内读写都走 latin1 所以「往返看着正常」，但 text/uri-list 是
    //   跨进程契约（RFC 2483），别的程序按 UTF-8 解析拿到的就是坏路径。
    QMimeData md;
    const QString cnPath = S("/tmp/\u8d34\u7eb8.png");     // /tmp/贴纸.png
    QList<QUrl> urls;
    urls.append(qUrlFromLocalFile(cnPath));
    md.setUrls(urls);

    // 逐字节检查写出去的 text/uri-list：必须是 cnPath 的 UTF-8 编码，
    // 不能出现 '?'（0x3F）
    const QByteArray raw = md.data(S("text/uri-list"));
    // ⚠ Qt3 的 QByteArray 就是 QMemArray<char>（**只有 size()**）；length()
    //   与 utf8() 是 QCString 这一层才加的。所以这里必须接成 QCString，
    //   否则切片到 QByteArray 后 length() 编译不过（踩过）。
    const QCString wantUtf8 = (S("file://") + cnPath).utf8();
    REQUIRE(raw.size() == wantUtf8.length() + 2);          // + CRLF
    for (int i = 0; i < (int)wantUtf8.length(); ++i)
        CHECK(raw.data()[i] == wantUtf8.data()[i]);
    CHECK(raw.data()[raw.size() - 2] == '\r');
    CHECK(raw.data()[raw.size() - 1] == '\n');

    // 往返也要拿回原路径
    const QList<QUrl> back = md.urls();
    REQUIRE(back.count() == 1);
    CHECK(qUrlToLocalFile(back.at(0)) == cnPath);
}

TEST_CASE("QMimeData: text/uri-list 不得含尾 NUL（QCString::size 陷阱回归）")
{
    // ⚠ 第二个真实 bug 的回归锁。Qt3 的 QCString::size() == length() + 1
    //   （**含尾 NUL**，实测 QCString("abc").size()==4），拿 size() 当拷贝
    //   长度会把那个 NUL 一起写进 text/uri-list，在 CRLF 前多出 0x00。
    //   症状：别的程序把 "\0" 当路径的一部分，路径失效。
    QMimeData md;
    QList<QUrl> urls;
    urls.append(qUrlFromLocalFile(S("/tmp/abc.png")));
    md.setUrls(urls);

    const QByteArray raw = md.data(S("text/uri-list"));
    for (int i = 0; i < (int)raw.size(); ++i)
        CHECK(raw.data()[i] != '\0');
    CHECK_EQ(raw.size(), (int)strlen("file:///tmp/abc.png") + 2);
}

TEST_CASE("QMimeData: urls() 解析外部写入的 uri-list（注释/空行/CRLF/非 ASCII）")
{
    // 模拟别的程序（文件管理器、Qt5/6）写进来的 text/uri-list，
    // 按 RFC 2483 用 UTF-8。urls() 必须：
    //   · 跳过 '#' 注释行
    //   · 跳过空行
    //   · 同时吃 CRLF 和裸 LF
    //   · 用 fromUtf8 解码（本 shim 修复前是 fromLatin1，中文会变乱码）
    QMimeData md;
    const char* body =
        "# a comment line\r\n"
        "\r\n"
        "file:///tmp/a.png\r\n"
        "file:///tmp/贴纸.png\n"                  // 贴纸.png
        "file:///tmp/b.png\r\n";
    QByteArray list = qbaFromRaw(body, (int)strlen(body));
    md.setData(S("text/uri-list"), list);

    const QList<QUrl> us = md.urls();
    REQUIRE(us.count() == 3);                      // 注释与空行都不算
    CHECK(qUrlToLocalFile(us.at(0)) == S("/tmp/a.png"));
    CHECK(qUrlToLocalFile(us.at(1)) == S("/tmp/\u8d34\u7eb8.png"));
    CHECK(qUrlToLocalFile(us.at(2)) == S("/tmp/b.png"));
}

TEST_CASE("QMimeData: 空/缺 uri-list 时 urls() 返回空列表")
{
    QMimeData md;
    CHECK_EQ(md.urls().count(), 0);
    md.setData(S("text/uri-list"), B(""));
    CHECK_EQ(md.urls().count(), 0);
    // 只有注释和空行也算空
    md.setData(S("text/uri-list"), qbaFromRaw("# only a comment\n\n", 18));
    CHECK_EQ(md.urls().count(), 0);
}

// ═══════════════════════════════════════════════════════════════════════
// QUrl helper
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("qUrlToLocalFile/qUrlFromLocalFile 往返")
{
    const QString p = S("/tmp/stickers/\u8d34\u7eb8.png");
    const QUrl u = qUrlFromLocalFile(p);
    CHECK(qUrlToLocalFile(u) == p);
    // Qt3 的 QUrl(path) 对裸路径默认 protocol=file，故 isLocalFile() 成立
    CHECK(u.isLocalFile());
}

// ═══════════════════════════════════════════════════════════════════════
// Qt3Clipboard / QGuiApplication —— 需要 X11
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("QGuiApplication: clipboard() 是单例")
{
    if (!haveX11()) return;
    ensureApp();
    Qt3Clipboard* a = QGuiApplication::clipboard();
    Qt3Clipboard* b = QGuiApplication::clipboard();
    REQUIRE(a != 0);
    CHECK(a == b);          // 必须同一实例，否则内嵌 m_read 缓存会分叉
}

TEST_CASE("Qt3Clipboard: setMimeData → mimeData 往返（走真实 X11 剪贴板）")
{
    if (!haveX11()) return;
    ensureApp();
    Qt3Clipboard* cb = QGuiApplication::clipboard();
    REQUIRE(cb != 0);

    // ⚠ 所有权交给 Qt3 的 QClipboard::setData（实测它会在替换/clear 时
    //   delete 旧的），所以这里 new 之后**绝不能**自己 delete。
    QMimeData* md = new QMimeData;
    md->setData(S("text/plain"), B("hello-clip"));
    cb->setMimeData(md);

    QMimeData* rd = cb->mimeData();
    REQUIRE(rd != 0);
    CHECK(rd->data(S("text/plain")) == B("hello-clip"));
    CHECK(rd->hasFormat(S("text/plain")));
}

TEST_CASE("Qt3Clipboard: mimeData() 返回的是同一个内部缓冲，可重复读")
{
    // 回归点：mimeData() 每次都 m_read.clear() 后重填，并返回 &m_read。
    // 调用点（stickerstore.cpp:2607-2626）会连续 mimeData()->data(...) 多次，
    // 若指针每次不同或内容被清空，第二个字段就读不到了。
    if (!haveX11()) return;
    ensureApp();
    Qt3Clipboard* cb = QGuiApplication::clipboard();
    REQUIRE(cb != 0);

    QMimeData* md = new QMimeData;
    md->setData(S("text/plain"), B("multi"));
    md->setData(S("image/png"),  B("PNGDATA"));
    cb->setMimeData(md);

    QMimeData* r1 = cb->mimeData();
    QMimeData* r2 = cb->mimeData();
    CHECK(r1 == r2);                                  // 同一内部缓冲
    CHECK(r1->data(S("text/plain")) == B("multi"));    // 两次读都全
    CHECK(r1->data(S("image/png"))  == B("PNGDATA"));
}

TEST_CASE("Qt3Clipboard: setImage/image 往返且保留 alpha")
{
    if (!haveX11()) return;
    ensureApp();
    Qt3Clipboard* cb = QGuiApplication::clipboard();
    REQUIRE(cb != 0);

    QImage im = qImageNew32(2, 2);
    qImageFillTransparent(im);
    im.setPixel(0, 0, qRgba(0x11, 0x22, 0x33, 0xFF));
    cb->setImage(im);

    const QImage got = cb->image();
    CHECK_FALSE(got.isNull());
    CHECK_EQ(got.width(),  2);
    CHECK_EQ(got.height(), 2);
    CHECK_EQ(got.pixel(0, 0), (uint)qRgba(0x11, 0x22, 0x33, 0xFF));
}

TEST_CASE("Qt3Clipboard: setMimeData(imageData) 后 formats 含 image/png")
{
    // 动画路径（stickerstore.cpp:2813-2825）是把 PNG 字节作为**格式**
    // 写进 QMimeSource，而不是另调 setImage —— Qt3 的 setData 与 setImage
    // 互斥（后调用者清前者）。本条锁住「格式可见」这个前提。
    if (!haveX11()) return;
    ensureApp();
    Qt3Clipboard* cb = QGuiApplication::clipboard();
    REQUIRE(cb != 0);

    QImage im = qImageNew32(2, 2);
    qImageFillTransparent(im);
    im.setPixel(0, 0, qRgba(0x11, 0x22, 0x33, 0xFF));

    QMimeData* md = new QMimeData;
    md->setImageData(im);
    cb->setMimeData(md);

    QMimeData* rd = cb->mimeData();
    CHECK(rd->hasFormat(S("image/png")));
    const QByteArray png = rd->data(S("image/png"));
    REQUIRE(png.size() > 8);
    CHECK((unsigned char)png.data()[0] == 0x89);
    CHECK(png.data()[1] == 'P');
}

TEST_CASE("Qt3Clipboard: setMimeData(NULL) 是安全的空操作")
{
    if (!haveX11()) return;
    ensureApp();
    Qt3Clipboard* cb = QGuiApplication::clipboard();
    REQUIRE(cb != 0);
    cb->setMimeData(0);                 // 不得崩
    cb->setMimeData(NULL);
}

TEST_CASE("Qt3Clipboard: 连续替换剪贴板数据不崩（所有权交给 Qt3，无双删）")
{
    // ⚠ 若 shim 的 setMimeData 自己 delete 再交给 QClipboard，或反过来
    //   两者都 delete，就会双删 → 崩溃。这里连换三次逼出这个问题。
    if (!haveX11()) return;
    ensureApp();
    Qt3Clipboard* cb = QGuiApplication::clipboard();
    REQUIRE(cb != 0);

    for (int i = 0; i < 3; ++i) {
        QMimeData* md = new QMimeData;
        char buf[32];
        sprintf(buf, "round-%d", i);
        md->setData(S("text/plain"), qbaFromRaw(buf, (int)strlen(buf)));
        cb->setMimeData(md);           // 旧的那个由 Qt3 在替换时 delete
    }
    // 最后一份仍应读得到
    QByteArray last = cb->mimeData()->data(S("text/plain"));
    CHECK(last == B("round-2"));
}
