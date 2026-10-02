// stikcommon/test_qmimedatabase_shim.cpp —— QMimeDatabase/QMimeType 垫片契约（Qt3 单端）
//
// 被测对象：qmimedatabase_shim.h
//   QMimeDatabase::mimeTypeForFile / mimeTypeForName
//   QMimeType::isValid / name / comment
//   （间接覆盖 qMimeTypeFromExtension / qMimeSniffImage）
//
// 纯头文件、纯逻辑，**无需 QApplication、无需 X11**（只读文件头 64 字节）。
//
// ── 本文件锁定的优先级契约 ────────────────────────────────────────────────
//
// ⚠【扩展名 > 内容幻数 > 无效】这是 shim 明确对齐 Qt6 的地方，也是最容易
//   被后人「好心修成按内容判断」的地方 —— 那样会把下面几个实测事实反过来：
//     misnamed.gif  内容是 PNG    → 仍 image/gif   （扩展名赢）
//     real.apng     内容是普通 PNG → 仍 image/apng  （扩展名赢）
//     real.zzz      内容是 PNG    → image/png      （扩展名不认识，看内容）
//   对应 stickerstore.cpp 的 1860（probe 失败后宽松回退）与 1913（精化 mime）：
//   文件名是用户自己起的，不能因为内容看着像别的格式就改判用户文件的类型。

#include "doctest/doctest.h"
#include <qstring.h>
#include <qfileinfo.h>
#include "qmimedatabase_shim.h"
#include "qtemporaryfile_shim.h"

// ⚠ 本仓规范：非 ASCII 一律 UTF-8，不用 fromLatin1（AGENTS.md）。
inline QString S(const char* s) { return QString::fromUtf8(s); }

// 只含给定字节的临时文件；析构自动删除（QTemporaryFile autoRemove）。
//
// ⚠ 模板必须含至少 6 个连续 'X'：Qt3 的 QTemporaryFile 默认模板为空，
//   createUniqueFileName() 会直接 false 并返回空名。
// ⚠ 扩展名写在 XXXXXX **之后**：fillTemplate() 用 mid(xPos + 6) 保留尾部，
//   所以 `/tmp/x_XXXXXX.gif` 会生成 `/tmp/x_<6位>.gif`。这比「先建再改名」
//   干净 —— Qt3 的 QFile **根本没有 rename()**，改名那条路在 /opt/qt338sh
//   上走不通（已实测确认，见本文件历史）。
// ⚠ write() 写完必须 close()：QMimeDatabase::readHead() 是用**另一个**
//   QFile 句柄重新打开路径读盘（qmimedatabase_shim.h:187-195），而
//   QTemporaryFile 此刻仍开着、数据可能还在缓冲里。不 close 的话
//   readHead 读到 0 字节 → 内容幻数全部失配，测试会以「PNG 认不出来」
//   的假象失败（这正是本文件第一版的失败原因）。
struct TempFile {
    QTemporaryFile f;
    explicit TempFile(const char* suffix = "")
        : f(S("/tmp/qmimetest_XXXXXX") + S(suffix)) {}
    bool open() { return f.open(); }          // 空文件用例：建了不写
    bool write(const char* bytes, int n)
    {
        if (!f.open()) return false;
        const bool ok = (f.write(bytes, n) == (Q_LONG)n);
        f.close();                              // 必须：flush 后另一句柄才读得到
        return ok;
    }
    QString path() const { return f.name(); }
};

// 标准 PNG 文件头（含 IHDR，够 12 字节）。
static const char kPngHead[] = "\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR";

// ── 扩展名映射 ────────────────────────────────────────────────────────────
TEST_CASE("QMimeDatabase: 已知扩展名映射到正确 mime 与 comment")
{
    QMimeDatabase db;
    struct Case { const char* file; const char* mime; const char* comment; };
    const Case cases[] = {
        { "a.png",  "image/png",                "PNG image" },
        { "a.apng", "image/apng",               "Animated PNG image" },
        { "a.jpg",  "image/jpeg",               "JPEG image" },
        { "a.jpeg", "image/jpeg",               "JPEG image" },
        { "a.gif",  "image/gif",                "GIF image" },
        { "a.webp", "image/webp",               "WebP image" },
        { "a.svg",  "image/svg+xml",            "SVG image" },
        { "a.bmp",  "image/bmp",                "Windows BMP image" },
        { "a.ico",  "image/vnd.microsoft.icon", "Windows icon" },
    };
    for (int i = 0; i < (int)(sizeof(cases)/sizeof(cases[0])); ++i) {
        CAPTURE(cases[i].file);
        const QMimeType t = db.mimeTypeForFile(S(cases[i].file));
        CHECK(t.isValid());
        CHECK(t.name() == S(cases[i].mime));
        CHECK(t.comment() == S(cases[i].comment));
    }
}

TEST_CASE("QMimeDatabase: 扩展名大小写不敏感（多别名归一）")
{
    QMimeDatabase db;
    CHECK(db.mimeTypeForFile(S("A.PNG")).name() == S("image/png"));
    CHECK(db.mimeTypeForFile(S("a.PNG")).name() == S("image/png"));
    CHECK(db.mimeTypeForFile(S("a.jpe")).name()  == S("image/jpeg"));
    CHECK(db.mimeTypeForFile(S("a.jfif")).name() == S("image/jpeg"));
    CHECK(db.mimeTypeForFile(S("a.tiff")).name() == S("image/tiff"));
    CHECK(db.mimeTypeForFile(S("a.tif")).name()  == S("image/tiff"));
    CHECK(db.mimeTypeForFile(S("a.heic")).name() == S("image/heif"));
    CHECK(db.mimeTypeForFile(S("a.heif")).name() == S("image/heif"));
    CHECK(db.mimeTypeForFile(S("a.svgz")).name() == S("image/svg+xml-compressed"));
}

TEST_CASE("QMimeDatabase: 未知扩展名且文件不存在 → 无效")
{
    QMimeDatabase db;
    CHECK_FALSE(db.mimeTypeForFile(S("/tmp/definitely_absent_zzz.qqq")).isValid());
    CHECK_FALSE(db.mimeTypeForFile(S("/tmp/definitely_absent_noext")).isValid());
    // 默认构造的 QMimeType 也无效，且名字/注释为空
    const QMimeType def;
    CHECK_FALSE(def.isValid());
    CHECK(def.name().isEmpty());
    CHECK(def.comment().isEmpty());
}

// ── 优先级：扩展名赢过内容 ────────────────────────────────────────────────
TEST_CASE("QMimeDatabase: 扩展名优先于内容（gif 名装着 PNG 字节）")
{
    // Qt6/shared-mime-info 实测：已知扩展名的权重高于内容，故仍 image/gif。
    // 这是与 Qt6 对齐的关键，别改成按内容判。
    QMimeDatabase db;
    TempFile tf(".gif");
    REQUIRE(tf.write(kPngHead, (int)sizeof(kPngHead) - 1));

    const QMimeType t = db.mimeTypeForFile(tf.path());
    CHECK(t.isValid());
    CHECK(t.name() == S("image/gif"));      // 扩展名赢，不是 image/png
    CHECK(t.comment() == S("GIF image"));
}

TEST_CASE("QMimeDatabase: 扩展名优先于内容（apng 不因内容退化成 png）")
{
    QMimeDatabase db;
    TempFile tf(".apng");
    REQUIRE(tf.write(kPngHead, (int)sizeof(kPngHead) - 1));
    CHECK(db.mimeTypeForFile(tf.path()).name() == S("image/apng"));
}

// ── 优先级：扩展名不认识才看内容 ──────────────────────────────────────────
TEST_CASE("QMimeDatabase: 未知扩展名时按内容幻数识别")
{
    QMimeDatabase db;
    struct Case { const char* bytes; int n; const char* mime; };
    const Case cases[] = {
        { "\x89PNG\r\n\x1a\n",        8,  "image/png" },
        { "\xFF\xD8\xFF\xE0",         4,  "image/jpeg" },
        { "GIF89a\x01\x00",           8,  "image/gif" },
        { "GIF87a\x01\x00",           8,  "image/gif" },
        { "RIFF\x00\x00\x00\x00WEBPVP8 ", 16, "image/webp" },
        { "BM\x36\x00\x00\x00",       6,  "image/bmp" },
        { "II\x2a\x00\x08\x00",       6,  "image/tiff" },
        { "MM\x00\x2a\x00\x08",       6,  "image/tiff" },
        { "\x00\x00\x01\x00\x01",     5,  "image/vnd.microsoft.icon" },
        // ⚠ ftyp 三例必须写成两段字面量拼接：`"\x18ftyp..."` 会被编译器
        //   当成**一个**十六进制转义 —— 'f' 也是十六进制数字，`\x18f` = 0x18f！
        //   实测字节布局：
        //     "\x00\x00\x00\x18ftypavif"       → 00 00 00 8F 74 79 70 ...（错）
        //     "\x00\x00\x00\x18" "ftypavif"    → 00 00 00 18 66 74 79 ...（对）
        //   错版里 'f' 被吞掉，p+4 变成 "typa" 而非 "ftyp"，幻数匹配全废 ——
        //   且这种错**静默**：数组长度照样是 12，看不出异常。
        { "\x00\x00\x00\x18" "ftypavif", 12, "image/avif" },
        { "\x00\x00\x00\x18" "ftypheic", 12, "image/heif" },
        { "\x00\x00\x00\x18" "ftypmif1", 12, "image/heif" },
        { "qoif\x00\x00\x00\x01",     8,  "image/qoi" },
        { "8BPS\x00\x00\x00\x01",     8,  "image/vnd.adobe.photoshop" },
        { "v/1\x01\x00\x00",          5,  "image/x-exr" },
        { "<?xml version=\"1.0\"?><svg/>", 26, "image/svg+xml" },
        { "<svg xmlns=\"...\"/>",     18, "image/svg+xml" },
    };
    for (int i = 0; i < (int)(sizeof(cases)/sizeof(cases[0])); ++i) {
        TempFile tf(".zzz");           // 扩展名故意不认识 → 必须走内容
        REQUIRE(tf.write(cases[i].bytes, cases[i].n));
        // ⚠ 只 CAPTURE(i)：这个 doctest 版本（2.4.11）把 const char* 渲成
        //   裸指针地址（0x5644...）而非字符串，混进 CAPTURE 既无用又难读。
        CAPTURE(i);
        const QMimeType t = db.mimeTypeForFile(tf.path());
        CHECK(t.isValid());
        CHECK(t.name() == S(cases[i].mime));
    }
}

TEST_CASE("QMimeDatabase: 内容不是任何已知图片 → 无效")
{
    QMimeDatabase db;
    const char plain[] = "this is definitely a plain text file, not an image at all";
    TempFile tf(".qqq");
    REQUIRE(tf.write(plain, (int)sizeof(plain) - 1));
    CHECK_FALSE(db.mimeTypeForFile(tf.path()).isValid());
}

TEST_CASE("QMimeDatabase: 空文件 → 无效（不崩）")
{
    // 回归点：readHead 返回 0 时，match() 里两处 `if (n > 0 && ...)` 短路，
    // 直接落到 return QMimeType()。曾经担心 0 长度被当幻数读。
    QMimeDatabase db;
    TempFile tf(".zzz");
    REQUIRE(tf.open());      // 建了但一个字节没写
    CHECK_FALSE(db.mimeTypeForFile(tf.path()).isValid());
}

TEST_CASE("QMimeDatabase: 幻数被截短到门槛以下 → 无效（不能读越界）")
{
    // 回归点：qMimeSniffImage 每条规则都先查 n >= 该规则需要的长度。
    // 用 PNG（门槛 n>=8）做截断：只写 7 字节就认不出来。
    QMimeDatabase db;
    const char png7[] = "\x89PNG\r\n\x1a";        // 7 字节，差 1 字节
    TempFile tf(".qqq");
    REQUIRE(tf.write(png7, 7));
    CHECK_FALSE(db.mimeTypeForFile(tf.path()).isValid());

    // 对照：补上第 8 字节就该认出来
    TempFile tf2(".qqq");
    REQUIRE(tf2.write(kPngHead, 8));
    CHECK(db.mimeTypeForFile(tf2.path()).name() == S("image/png"));
}

TEST_CASE("QMimeDatabase: BMP 门槛就是 2 字节（BM 本身是完整签名）")
{
    // ⚠ BMP 的签名只有 "BM" 两个字节，规则写的是 n >= 2
    //   （qmimedatabase_shim.h 的 qMimeSniffImage）。所以 2 字节文件
    //   也会被判成 image/bmp —— 这是**有意为之**，不是漏写长度检查：
    //   BM 就是 BMP 的完整魔数，收紧反而会漏掉真 BMP。
    //   钉住它，免得后人以为这是 bug 而「修」成 n >= 6。
    QMimeDatabase db;
    const char bm[] = "BM";
    TempFile tf(".qqq");
    REQUIRE(tf.write(bm, 2));
    CHECK(db.mimeTypeForFile(tf.path()).name() == S("image/bmp"));

    // 对照：只写 1 字节 "B" → 不够门槛 → 无效
    TempFile tf2(".qqq");
    REQUIRE(tf2.write(bm, 1));
    CHECK_FALSE(db.mimeTypeForFile(tf2.path()).isValid());
}

// ── MatchMode ─────────────────────────────────────────────────────────────
TEST_CASE("QMimeDatabase: MatchExtension 忽略内容、MatchContent 忽略扩展名")
{
    QMimeDatabase db;
    TempFile tf(".gif");
    REQUIRE(tf.write(kPngHead, (int)sizeof(kPngHead) - 1));

    CHECK(db.mimeTypeForFile(tf.path(), QMimeDatabase::MatchDefault).name()   == S("image/gif"));
    CHECK(db.mimeTypeForFile(tf.path(), QMimeDatabase::MatchExtension).name() == S("image/gif"));
    CHECK(db.mimeTypeForFile(tf.path(), QMimeDatabase::MatchContent).name()   == S("image/png"));
}

TEST_CASE("QMimeDatabase: MatchContent 对非图片内容 → 无效，即使扩展名像图片")
{
    QMimeDatabase db;
    const char plain[] = "plain text only";
    TempFile tf(".gif");
    REQUIRE(tf.write(plain, (int)sizeof(plain) - 1));
    // 默认模式：扩展名赢 → gif
    CHECK(db.mimeTypeForFile(tf.path(), QMimeDatabase::MatchDefault).name() == S("image/gif"));
    // MatchContent：只看内容 → 无效
    CHECK_FALSE(db.mimeTypeForFile(tf.path(), QMimeDatabase::MatchContent).isValid());
}

TEST_CASE("QMimeDatabase: MatchExtension 对未知扩展名 → 无效（绝不回落到内容）")
{
    // MatchExtension 一旦回落到内容判断，该模式就失去意义。
    QMimeDatabase db;
    TempFile tf(".zzz");
    REQUIRE(tf.write(kPngHead, (int)sizeof(kPngHead) - 1));
    CHECK_FALSE(db.mimeTypeForFile(tf.path(), QMimeDatabase::MatchExtension).isValid());
    CHECK(db.mimeTypeForFile(tf.path(), QMimeDatabase::MatchContent).name() == S("image/png"));
}

// ── mimeTypeForName ───────────────────────────────────────────────────────
TEST_CASE("QMimeDatabase: mimeTypeForName —— 扩展名认得，全名只保证 valid")
{
    QMimeDatabase db;
    // 扩展名路径：查表命中，name 与 comment 都有
    CHECK(db.mimeTypeForName(S("png")).name() == S("image/png"));
    CHECK(db.mimeTypeForName(S("PNG")).name() == S("image/png"));
    CHECK_FALSE(db.mimeTypeForName(S("png")).comment().isEmpty());

    // 全名路径（含 '/'）：直接构造，**comment 为空**
    // ⚠ 这是 mimeTypeForName 的已知最小实现（shim 头注释已写明
    //   「未在 stickerstore 用到；给最小实现」），所以 comment 不做查询。
    const QMimeType full = db.mimeTypeForName(S("image/png"));
    CHECK(full.isValid());
    CHECK(full.name() == S("image/png"));
    CHECK(full.comment().isEmpty());
    CHECK(db.mimeTypeForName(S("image/gif")).isValid());

    // ⚠ 已知局限：任何**含 '/'** 的串都会被原样构造为 valid，不校验它
    //   真是合法 MIME 名。stickerstore 不走这条路径（只调 mimeTypeForFile），
    //   故按现状钉住；若将来有人开始调用 mimeTypeForName，这里就是提醒。
    CHECK(db.mimeTypeForName(S("application/x-nosuchthing")).isValid());

    // 不含 '/' 且查不到 → 无效（这条是真查表）
    CHECK_FALSE(db.mimeTypeForName(S("nonsense")).isValid());
    CHECK_FALSE(db.mimeTypeForName(S("qqq")).isValid());
}

// ── QFileInfo / QString 两个重载一致 ──────────────────────────────────────
TEST_CASE("QMimeDatabase: QFileInfo 与 QString 两个重载结果一致")
{
    QMimeDatabase db;
    TempFile tf(".zzz");
    REQUIRE(tf.write(kPngHead, (int)sizeof(kPngHead) - 1));
    CHECK(db.mimeTypeForFile(QFileInfo(tf.path())).name()
          == db.mimeTypeForFile(tf.path()).name());
}

// ── comment() 的调用前提（stickerstore.cpp:1862-1864）────────────────────
TEST_CASE("QMimeDatabase: 只有走扩展名查表才承诺 comment，调用方才读")
{
    // stickerstore.cpp:1862-1864 只在 name() 以 "image/" 开头时读 comment()。
    // 而 mimeTypeForFile 的 comment 只在**扩展名命中查表**时才有 —— 全名路径
    // （mimeTypeForName("image/png")）构造的是空 comment。
    // 对该调用点无害：它读的是 mimeTypeForFile 的结果，那条路径必然过查表。
    // 钉住这个前提，免得后人误以为「image/* 就一定有 comment」。
    QMimeDatabase db;
    const QMimeType byExt = db.mimeTypeForFile(S("a.png"));
    CHECK(byExt.name().startsWith(S("image/")));
    CHECK_FALSE(byExt.comment().isEmpty());   // 扩展名路径：有注释

    // 内容识别路径也带 comment（qMimeSniffImage 一并填了）
    TempFile tf(".zzz");
    REQUIRE(tf.write(kPngHead, 8));
    const QMimeType byContent = db.mimeTypeForFile(tf.path());
    CHECK(byContent.name().startsWith(S("image/")));
    CHECK_FALSE(byContent.comment().isEmpty());

    // 非图片名不承诺 comment，且 stickerstore 的 if 会跳过读取
    const QMimeType nonImage = db.mimeTypeForName(S("text/plain"));
    CHECK(nonImage.isValid());
    CHECK(nonImage.comment().isEmpty());
}
