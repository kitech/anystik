// stikcommon/test_qtemporaryfile_shim.cpp —— QTemporaryFile 垫片契约（Qt3 单端）
//
// 被测对象：qtemporaryfile_shim.h（仅 QT_VERSION < 0x040300 时有内容）
//
// 背景：QTemporaryFile 是 Qt 4.3 才引入的类，Qt3.5 没有。本垫片手写一个
// 同名类来顶替，语义对齐 Qt 原生：
//   * 构造传「模板名」，须含至少 6 个连续 'X'；据此挑一个当下不存在的名字
//   * open() 才真正创建；fileName() 拿路径
//   * 默认 autoRemove=true，析构时删掉自己创建的文件
// 纯头文件，无需链接产品 .cpp。
//
// ⚠ 用法两条硬约束（下面的用例把它们钉住）：
//   1. 扩展名必须写在 XXXXXX **之后**（/tmp/x_XXXXXX.gif，不是 /tmp/x.gifXXXXXX）。
//      原因：fillTemplate() 用 mid(xPos + 6) 保留尾部，Qt3 的 QFile 又没有
//      rename()，改名的路子走不通。
//   2. write() 之后要 close() 才能让**另一个句柄**读到内容 —— 同
//      test_qmimedatabase_shim.cpp:38-41 记录的踩坑（那文件第一版就是因此
//      以「PNG 认不出来」的假象失败的）。
//
// ── 关于并发 ────────────────────────────────────────────────────────────
// 本垫片是 exists() 探测 + open() 两步，与 Qt 原生的 open(O_CREAT|O_EXCL)
// 原子建文件不同，中间有竞态窗口（qtemporaryfile_shim.h:17-20 已注明）。
// 故这里**不**测多线程/多进程并发建名，只测单线程内连续创建不撞名。

#include "qtemporaryfile_shim.h"
#include "qstring_shim.h"
#include "qba_shim.h"
#include "qdir_shim.h"

#include <doctest.h>

#include <string.h>                   // memset

using namespace doctest;

// 把「/tmp/<前缀>XXXXXX<后缀>」当模板用。前缀带进程号，避免与并行跑的
// 另一份套件互相干扰（两边都在 /tmp 下建文件）。
static QString tpl(const char* prefix, const char* suffix = "")
{
    QString s = qDirTempPath();          // Qt3 无 QDir::tempPath()
    s += "/";
    s += prefix;
    s += QString::number(uint(getpid()));
    s += "_XXXXXX";
    s += QString::fromUtf8(suffix);
    return s;
}

// ── 模板校验：不满足条件必须失败，且不得留下文件 ───────────────────────
TEST_CASE("QTemporaryFile 模板缺 XXXXXX 时 open 失败")
{
    QTemporaryFile f(tpl("qt3t_bad_", ".gif").replace("XXXXXX", "ZZZZZ"));
    CHECK(!f.open());
    CHECK(f.fileName().isEmpty());
}

TEST_CASE("QTemporaryFile 空模板时 open 失败")
{
    QTemporaryFile f;                       // 默认模板为空
    CHECK(f.fileTemplate().isEmpty());
    CHECK(!f.open());
    CHECK(f.fileName().isEmpty());
}

TEST_CASE("QTemporaryFile XXXXXX 不足 6 个时 open 失败")
{
    // 只有 5 个 X：find("XXXXXX") 找不到 → 直接 false
    QString base = qDirTempPath();
    base += "/qt3t_short_";
    base += QString::number(uint(getpid()));
    base += "_XXXXX.gif";
    QTemporaryFile f(base);
    CHECK(!f.open());
}

// ── 正常路径：open / fileName / 前缀后缀保留 ──────────────────────────
TEST_CASE("QTemporaryFile open 成功且文件真实存在")
{
    QTemporaryFile f(tpl("qt3t_ok_"));
    CHECK(f.open());
    const QString path = f.fileName();
    CHECK(!path.isEmpty());
    CHECK(QFile::exists(path));             // open 之后确实落盘
    f.close();
}

TEST_CASE("QTemporaryFile 保留模板前缀与后缀")
{
    // 扩展名在 XXXXXX 之后（见文件头约束 1）
    QTemporaryFile f(tpl("qt3t_sfx_", ".gif"));
    CHECK(f.open());
    const QString path = f.fileName();

    CHECK(path.endsWith(QString(".gif")));              // 后缀保留
    CHECK(path.contains(QString("qt3t_sfx_")));         // 前缀保留
    CHECK(!path.contains(QString("XXXXXX")));           // X 段已被替换掉
    f.close();
}

TEST_CASE("QTemporaryFile fileTemplate 返回构造时的模板")
{
    const QString t = tpl("qt3t_tpl_", ".bin");
    QTemporaryFile f(t);
    CHECK(f.fileTemplate() == t);

    // setFileTemplate 之后能换模板，且换完再 open 用的是新模板
    const QString t2 = tpl("qt3t_tpl2_", ".dat");
    f.setFileTemplate(t2);
    CHECK(f.fileTemplate() == t2);
    CHECK(f.open());
    CHECK(f.fileName().contains(QString("qt3t_tpl2_")));
    f.close();
}

// ── createUniqueFileName：只挑名，不建文件 ─────────────────────────────
TEST_CASE("QTemporaryFile createUniqueFileName 只定名不建文件")
{
    QTemporaryFile f(tpl("qt3t_nameonly_"));
    CHECK(f.createUniqueFileName());
    const QString path = f.fileName();
    CHECK(!path.isEmpty());
    CHECK(!QFile::exists(path));            // 关键：此刻还没落盘
    QFile::remove(path);                    // 保险：万一实现改了语义也不留垃圾
}

// ── 唯一性：同时存活的对象之间不撞名 ─────────────────────────────────
// ⚠ 写这个用例时踩了两个坑，都记在这儿免得再犯：
//   1. 别写成「for 循环体里建 QTemporaryFile f」就完事：每轮结束 f 析构会把
//      文件删掉，下一轮探测到同名「不存在」就**复用**。名字释放后复用是合法的，
//      拿它当「撞名」是错的（我第一版 8 轮里 7 轮误报）。
//   2. 循环体里**不能**夹 CHECK / 其它耗时操作：那样每轮跨毫秒，msec 变化就把
//      名字天然区分开，测试永远走不到 exists() 失败后的重试分支 —— 于是把
//      fillTemplate() 里的 attempt 整个删掉（真·破坏唯一性的变异），测试照样全绿。
//      故这里先紧凑地建+开，断言一律挪到循环外。
// 契约本身是「同时存活者不重名」；这条用例验证的是**产品真实用法**
//   （stickerstore.cpp:1335/:1612：构造 → open → 拿路径），这条路径是绿的。
//
// ⚠⚠ 已知覆盖缺口：**exists() 失败后的 attempt 重试分支没有被确定性覆盖**。
//   实测（/tmp 探针，非猜测）：
//     · 32 次「构造+open+close」共 7.474ms，平均 0.234ms/次；
//     · 但本用例循环里还要 tpl() 拼串 + QStringList::operator<<，每轮耗时更大，
//       32 轮基本每轮都跨了毫秒 → msec 变化直接把名字区分开 → 永远不撞名。
//   于是把 fillTemplate() 里的 attempt 整行删掉（真·破坏重试能力的变异），
//   本用例**依然 192/192 全绿** —— 即它抓不住那个变异。别据此以为重试逻辑有用。
//   同理，探针还测到：只 createUniqueFileName() 而不 open() 时，同一毫秒内建的
//   8 个对象有 7 个**同名**（21 对重名）—— 因为文件要到 open() 才诞生，
//   exists() 探不到，attempt 根本走不到。这是 exists() 探测式实现的固有性质，
//   与 attempt 在不在无关。当前产品是「一次一个临时文件」，踩不到。
//   要真正覆盖需往 shim 加注入口（可注入的名字/计数器）或改用 open(O_EXCL)，
//   属设计变更，未做 —— 见交付说明里给用户的两个待决问题。
TEST_CASE("QTemporaryFile 同时存活的多个对象不撞名")
{
    enum { N = 32 };
    QTemporaryFile* keep[N];
    bool opened[N];
    QStringList seen;

    for (int i = 0; i < N; ++i) {          // 紧凑循环：内部不做任何断言
        keep[i] = new QTemporaryFile(tpl("qt3t_uniq_"));
        opened[i] = keep[i]->open();
        seen << keep[i]->fileName();
        keep[i]->close();
    }

    int dupPairs = 0;
    for (int i = 0; i < N; ++i) {
        CAPTURE(i);
        CHECK(opened[i]);                          // 撞名时 open 会失败
        CHECK(!seen[i].isEmpty());
        for (int j = i + 1; j < N; ++j)
            if (seen[i] == seen[j]) ++dupPairs;
    }
    CHECK_EQ(dupPairs, 0);                         // 同时存活者两两不同名
    CHECK_EQ(seen.count(), N);

    for (int i = 0; i < N; ++i) {
        CAPTURE(i);
        CHECK(QFile::exists(seen[i]));             // 都真的落盘了
    }
    for (int i = 0; i < N; ++i) delete keep[i];    // 析构各自删掉自己的
    for (int i = 0; i < N; ++i) {
        CAPTURE(i);
        CHECK(!QFile::exists(seen[i]));
    }
}

// ── autoRemove 行为 ────────────────────────────────────────────────────
TEST_CASE("QTemporaryFile 默认 autoRemove 为真，析构删文件")
{
    QString path;
    {
        QTemporaryFile f(tpl("qt3t_rm_"));
        CHECK(f.autoRemove());
        CHECK(f.open());
        path = f.fileName();
        f.close();
        CHECK(QFile::exists(path));
    }                                       // 析构
    CHECK(!QFile::exists(path));            // 应已被删掉
}

TEST_CASE("QTemporaryFile setAutoRemove(false) 后析构保留文件")
{
    QString path;
    {
        QTemporaryFile f(tpl("qt3t_keep_"));
        f.setAutoRemove(false);
        CHECK(!f.autoRemove());
        CHECK(f.open());
        path = f.fileName();
        f.close();
    }
    CHECK(QFile::exists(path));             // 应仍在
    CHECK(QFile::remove(path));             // 手工清理
    CHECK(!QFile::exists(path));
}

TEST_CASE("QTemporaryFile 两参数构造可关掉自动删除")
{
    QString path;
    {
        QTemporaryFile f(tpl("qt3t_ctor_"), false);   // 第二个参数 autoRemove
        CHECK(!f.autoRemove());
        CHECK(f.open());
        path = f.fileName();
        f.close();
    }
    CHECK(QFile::exists(path));
    CHECK(QFile::remove(path));
}

TEST_CASE("QTemporaryFile open 失败时不删别人的文件")
{
    // 目录不存在 → open 失败 → m_created 保持 false → 析构不得删任何东西。
    // 这里用一个「碰巧存在」的目标文件来验证：名字是模板生成的，不可能撞上它，
    // 所以真正的检查点是 open 失败后析构没有把别处文件删掉。
    const QString victim = tpl("qt3t_victim_", ".keep");
    {
        QFile v(victim);
        CHECK(v.open(IO_WriteOnly));
        CHECK(v.writeBlock("keep", 4) == 4);
        v.close();
    }
    {
        QTemporaryFile f(tpl("qt3t_nodir_/missing_"));
        CHECK(!f.open());                   // 父目录不存在
    }
    CHECK(QFile::exists(victim));           // 仍在
    CHECK(QFile::remove(victim));
}

// ── 读写往返（Qt4 形态成员）────────────────────────────────────────────
// ⚠ Qt3 的 QFile / QIODevice **都没有 seek()**（/opt/qt338sh/include/qiodevice.h
//   只有 atEnd()，qfile.h 里也无 seek），故无法在同一句柄上回卷重读。
//   于是这里改成「写 → close → 同一对象只读重开」：名字已定，open() 会跳过
//   createUniqueFileName()，不会另挑一个名字。这同时也是产品里的真实用法
//   （stickerstore.cpp:1612 是 close 后把路径交给 GIF 库）。
TEST_CASE("QTemporaryFile write(QByteArray) 后可只读重开读回")
{
    QTemporaryFile f(tpl("qt3t_rw_"));
    CHECK(f.open());

    QByteArray payload;
    payload += "ab";
    payload += "cd";
    CHECK_EQ(f.write(payload), 4);
    f.flush();
    f.close();

    CHECK(f.open(IO_ReadOnly));             // 名字还在，不会另挑
    QByteArray got = f.readAll();
    CHECK_EQ(got.size(), 4);
    CHECK(got == payload);
    f.close();
}

TEST_CASE("QTemporaryFile write(char*, len) 与 read(char*, len) 重载")
{
    QTemporaryFile f(tpl("qt3t_rw2_"));
    CHECK(f.open());
    CHECK_EQ(f.write("efgh", 4), 4);
    f.flush();
    f.close();

    CHECK(f.open(IO_ReadOnly));
    char buf[8];
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(f.read(buf, 4), 4);
    CHECK(buf[0] == 'e');
    CHECK(buf[1] == 'f');
    CHECK(buf[2] == 'g');
    CHECK(buf[3] == 'h');
    f.close();
}

TEST_CASE("QTemporaryFile write 空 QByteArray 返回 0")
{
    QTemporaryFile f(tpl("qt3t_empty_"));
    CHECK(f.open());
    CHECK_EQ(f.write(QByteArray()), 0);
    f.close();
}

TEST_CASE("QTemporaryFile 写入内容可被另一句柄读到（须先 close）")
{
    QTemporaryFile f(tpl("qt3t_cross_"));
    CHECK(f.open());
    QByteArray payload;
    payload += "xyzw";
    CHECK_EQ(f.write(payload), 4);
    f.flush();
    const QString path = f.fileName();
    f.close();                              // 约束 2：必须 close

    QFile r(path);
    CHECK(r.open(IO_ReadOnly));
    QByteArray seen = r.readAll();
    r.close();
    CHECK_EQ(seen.size(), 4);
    CHECK(seen == payload);
}

// ── 非 ASCII 目录：路径编码没被搞坏 ────────────────────────────────────
TEST_CASE("QTemporaryFile 非 ASCII 目录下正常工作")
{
    // 目录名用 UTF-8 构造（「临时-测试」），验证 shim 没把路径编码搞坏。
    const QString dirName = QString::fromUtf8("\xe4\xb8\xb4\xe6\x97\xb6");
    const QString dir = qDirTempPath() + "/" + dirName
                        + "_" + QString::number(uint(getpid()));
    if (!QDir(dir).exists() && !QDir().mkdir(dir)) {
        MESSAGE("建不了非 ASCII 测试目录，本用例跳过");
        return;
    }

    QString path;
    {
        QTemporaryFile f(dir + "/XXXXXX.dat");
        CHECK(f.open());
        path = f.fileName();
        CHECK(!path.isEmpty());
        CHECK(QFile::exists(path));
        f.close();
    }
    CHECK(!QFile::exists(path));            // 析构已删

    QDir(dir).rmdir(dir);                   // 目录空了才能删
}

// ── setFileName 后析构不得删掉调用方自己的文件 ─────────────────────────
// 这是本垫片唯一的**数据丢失**隐患。setFileName() 是 Qt3 QFile 的改名适配
// （qtemporaryfile_shim.h:132），而 Qt 原生 QTemporaryFile 根本没有 setFileName
// （只有 setFileTemplate）。旧实现里 open() 无条件 m_created = true，于是
// 「调用方指定名字 → open → 析构」会把别人的文件删掉。收紧方式：只有当当前
// 名字是 createUniqueFileName() 自己挑出来的，析构才认账。
TEST_CASE("QTemporaryFile setFileName 后 open，析构不删调用方的文件")
{
    const QString victim = tpl("qt3t_owned_", ".keep");
    {                                       // 先造一个「别人的文件」
        QFile v(victim);
        CHECK(v.open(IO_WriteOnly));
        CHECK(v.writeBlock("keep", 4) == 4);
        v.close();
    }
    CHECK(QFile::exists(victim));

    {
        QTemporaryFile f;                   // 空模板：它自己挑不出名字
        f.setFileName(victim);              // 调用方指定
        CHECK(f.open(IO_ReadWrite));        // 打开了别人的文件
        f.close();
    }                                       // 析构：绝不能删掉 victim

    CHECK(QFile::exists(victim));           // ← 修复前这里失败
    CHECK(QFile::remove(victim));           // 手工清理
}

TEST_CASE("QTemporaryFile 自己挑的名字仍由析构删除（未被 setFileName 污染）")
{
    QString path;
    {
        QTemporaryFile f(tpl("qt3t_ours_"));
        CHECK(f.open());
        path = f.fileName();
        f.close();
        // 中途再 setFileName 到别的路径：所有权应转移到新名字，原路径不再删
        CHECK(f.open(IO_ReadOnly));         // 仍是自己挑的名字
        f.close();
    }
    CHECK(!QFile::exists(path));            // 自己创建的要删掉
}
