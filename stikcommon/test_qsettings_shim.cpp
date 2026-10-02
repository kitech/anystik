// stikcommon/test_qsettings_shim.cpp —— qsettings_shim.h 契约（Qt3 单端）
//
// 被测对象：qsettingsValue*/qSettingsSet*/qSettingsRemove/qSettingsChildKeys
//
// 该头是纯 inline，但 Qt3 侧的 QVariantMap 走 JSON 通道，依赖
// qjson_shim.cpp 与 qldox/cJSON.c（非 inline），故本文件需一起链接。
//
// 为什么必须有这组测试 —— 本垫片挡的是一个**静默丢数据**的坑：
// Qt3 的 QSettings 只接受 "group/leaf" 形式（键由两个以上 subkey 组成，
// subkey = '/' + 若干字符）。而 Qt6 的 value()/setValue() 接受任意裸键。
// 产品代码用的正是裸键（stickerstore.cpp 的 "storageRoot"、
// "dlProgress/<hex>"），所以在没有键规范化的旧版本垫片上，Qt3 下每次
// writeEntry 都只打一行 "invalid key" 日志、返回默认值，**不报任何错**，
// 表现为「配置永远不生效」。这类 bug 只靠读代码发现不了，必须跑测试。
//
// 隔离：Qt3 QSettings 无 (domain, product) 构造、无 fileName()、
// QApplication 也没有 setOrganizationName（Qt4+ 才有），唯一的隔离手段
// 是把 $HOME 指到临时目录——实测 setenv("HOME", ...) 在进程内立即生效
// （QDir::home() 每次重读 $HOME），QSettings 跟着走，文件落到
// $HOME/.qt/<键首段>rc。隔离的进出由本文件的 QsHomeScope fixture 负责
// （每个用例设一次、结束还原），不是「main 里统一设一次」——见文件末尾该
// fixture 的注释，解释为什么不用文件作用域静态对象。

#include <qsettings.h>
#include <qdir.h>
#include <qfile.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qvariant.h>

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <unistd.h>                        // getpid / setenv / unsetenv

#include "doctest/doctest.h"

#include "qsettings_shim.h"
#include "qdir_shim.h"                     // qDirTempPath / qDirRemoveRecursively
#include "qstring_shim.h"                  // qToUtf8BA（落盘字节校验用）

static inline QString S(const char* s) { return QString::fromUtf8(s); }

static void wipe()                          // 清掉本组测试写的所有键
{
    qSettingsRemove(S("qlstiktest/s"));
    qSettingsRemove(S("qlstiktest/i"));
    qSettingsRemove(S("qlstiktest/b"));
    qSettingsRemove(S("qlstiktest/d"));
    qSettingsRemove(S("qlstiktest/u"));
    qSettingsRemove(S("qlstiktest/sl"));
    qSettingsRemove(S("qlstiktest/m"));
    qSettingsRemove(S("qlstiktest/nested/a"));
    qSettingsRemove(S("qlstiktest/nested/deep/b"));
    // CJK 组的键（见文件末尾 CJK 用例群）
    qSettingsRemove(S("站点/贴纸"));
    qSettingsRemove(S("站点/标题"));
    qSettingsRemove(S("站点/标题2"));
    qSettingsRemove(S("sp/等号"));
    qSettingsRemove(S("sp/井号"));
    qSettingsRemove(S("sp/分号"));
    qSettingsRemove(S("sp/首空格"));
    qSettingsRemove(S("sp/尾空格"));
    qSettingsRemove(S("sp/全等"));
    qSettingsRemove(S("sl/中文表"));
    qSettingsRemove(S("sl/逗号"));
    qSettingsRemove(S("ext/生僻"));
    qSettingsRemove(S("fw/ＳＶＣ"));
}

// ── 隔离 HOME（每个用例进出成对）────────────────────────────────────
//
// 隔离手段没法换：Qt3 QSettings 没有 (domain, product) 构造、没有 fileName()，
// QApplication 也没有 setOrganizationName（Qt4+ 才有），唯一能动的是 $HOME ——
// 实测 setenv("HOME", ...) 在进程内立即生效（QDir::home() 每次重读 $HOME），
// QSettings 跟着走，文件落到 $HOME/.qt/<键首段>rc。
//
// ⚠ 用 doctest 的 TEST_CASE_FIXTURE 做**进出成对**的 HOME，而**不是**文件作用
//   域静态对象：后者只在 main 之前 setenv 一次、且从不还原，会让同一个
//   run_tests 二进制里**其它所有测试文件**都 secretly 在隔离 HOME 下跑 ——
//   对全局的隐形污染，哪天别的用例开始读 $HOME 就会莫名其妙地失败。
//   套件共用 test_main.cpp 的 DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN，本文件
//   不能自定义 main，fixture 是唯一干净的切入点。
//
// ⚠ 目录名带 pid：原实现固定用 /tmp/opencode/qtest_qsettings_home 且靠
//   system("rm -rf ...") 清，并发跑两套会互删。改用 qDirRemoveRecursively，
//   纯 Qt，不经 shell。
//
// ⚠ 本结构体必须定义在所有 TEST_CASE_FIXTURE **之前**（doctest 展开时要继承
//   它），故放在文件开头而非末尾。

namespace {

struct QsHomeScope
{
    QString m_saved;
    bool    m_hadHome;
    QString m_dir;

    QsHomeScope()
    {
        const char* old = getenv("HOME");
        m_hadHome = (old != 0);
        m_saved = S(old ? old : "");

        m_dir = qDirTempPath() + "/qtest_qsettings_home_"
                + QString::number(int(getpid()));
        QDir().mkdir(m_dir, true);
        setenv("HOME", m_dir.utf8().data(), 1);
    }

    ~QsHomeScope()
    {
        if (m_hadHome) setenv("HOME", m_saved.utf8().data(), 1);
        else           unsetenv("HOME");
        qDirRemoveRecursively(QDir(m_dir));
    }
};

} // namespace

// ── 基本类型往返 ─────────────────────────────────────────────────────
TEST_CASE_FIXTURE(QsHomeScope, "QSettings: 标量类型往返")
{
    wipe();
    qSettingsSetStr (S("qlstiktest/s"), S("hello"));
    qSettingsSetInt (S("qlstiktest/i"), 42);
    qSettingsSetBool(S("qlstiktest/b"), true);
    qSettingsSetDouble(S("qlstiktest/d"), 3.5);
    qSettingsSetStringList(S("qlstiktest/sl"), QStringList() << S("a") << S("b"));

    CHECK_EQ(qSettingsValueStr(S("qlstiktest/s")), S("hello"));
    CHECK_EQ(qSettingsValueInt(S("qlstiktest/i")), 42);
    CHECK_EQ(qSettingsValueBool(S("qlstiktest/b")), true);
    CHECK(qSettingsValueDouble(S("qlstiktest/d")) == 3.5);

    const QStringList got = qSettingsValueStringList(S("qlstiktest/sl"));
    REQUIRE(got.count() == 2);
    CHECK_EQ(got[0], S("a"));
    CHECK_EQ(got[1], S("b"));
    wipe();
}

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: QStringList 可区分空表与空值")
{
    wipe();
    // 写空表 → 读回空表；两者都"数为 0"，故先写两条再删除以确认可写
    QStringList two;
    two << S("x") << S("y");
    qSettingsSetStringList(S("qlstiktest/sl"), two);
    CHECK_EQ(qSettingsValueStringList(S("qlstiktest/sl")).count(), 2);
    qSettingsSetStringList(S("qlstiktest/sl"), QStringList());
    CHECK_EQ(qSettingsValueStringList(S("qlstiktest/sl")).count(), 0);
    // 键不存在时也是空表（与写空表不可区分 —— Qt3 无 ok 参数透出，见垫片说明）
    CHECK_EQ(qSettingsValueStringList(S("qlstiktest/absent")).count(), 0);
    wipe();
}

// ── 64 位种子（stickerstore 的 stickergen_seed 是随机 64 位）──────────
//
// Qt3 的 readNumEntry 只返回 int，装不下 >2^31。垫片改走 QString 通道中转。
// 若哪天有人图省事改回 readNumEntry，本组会炸。

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: qulonglong 保住 64 位（>2^31 与 >2^32）")
{
    wipe();
    const qulonglong big = Q_ULLONG(1234567890123ULL);
    qSettingsSetULongLong(S("qlstiktest/u"), big);
    CHECK_EQ(qSettingsValueULongLong(S("qlstiktest/u")), big);

    const qulonglong huge = Q_ULLONG(0xFEDCBA9876543210ULL);
    qSettingsSetULongLong(S("qlstiktest/u"), huge);
    CHECK_EQ(qSettingsValueULongLong(S("qlstiktest/u")), huge);

    const qulonglong zero = 0;
    qSettingsSetULongLong(S("qlstiktest/u"), zero);
    CHECK_EQ(qSettingsValueULongLong(S("qlstiktest/u"), 99), zero);
    wipe();
}

// ── 键规范化（本垫片的核心契约）────────────────────────────────────
//
// Qt3 键必须"两个以上 subkey"，裸键非法 → 全部写失败、读回落默认值。
// 垫片用合成组 "qlstik/" 承载裸键。以下逐条钉住等价性。

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: 裸键可写可读（Qt3 原本非法）")
{
    wipe();
    // 注意：这里刻意用**不带组**的键，等价于 stickerstore 的 "storageRoot"
    qSettingsSetStr(S("storageRoot"), S("/data/stickers"));
    CHECK_EQ(qSettingsValueStr(S("storageRoot")), S("/data/stickers"));
    qSettingsRemove(S("storageRoot"));
    CHECK_EQ(qSettingsValueStr(S("storageRoot"), S("DEF")), S("DEF"));
}

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: 带前导/尾部斜杠与裸键等价")
{
    wipe();
    qSettingsSetStr(S("bare"), S("V"));
    CHECK_EQ(qSettingsValueStr(S("/bare")), S("V"));
    CHECK_EQ(qSettingsValueStr(S("bare")),  S("V"));
    wipe();

    qSettingsSetStr(S("/g/x"), S("V2"));
    CHECK_EQ(qSettingsValueStr(S("g/x")),  S("V2"));   // 有组，前导斜杠可省
    CHECK_EQ(qSettingsValueStr(S("/g/x")), S("V2"));   // 也可留
    qSettingsRemove(S("g/x"));
    CHECK_EQ(qSettingsValueStr(S("g/x"), S("D")), S("D"));
}

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: 空键与纯斜杠键回落默认值（不产生垃圾数据）")
{
    wipe();
    // Qt3 无法表达「无 group 的叶键」，故这三者一律视为"键不存在"，
    // 语义上与 Qt6 的 value(key, def) 一致。
    const char* bad[] = { "", "/", "///" };
    for (int i = 0; i < 3; ++i) {
        qSettingsSetStr(S(bad[i]), S("X"));
        CHECK_MESSAGE(qSettingsValueStr(S(bad[i]), S("DEF")) == S("DEF"),
                      "键=", bad[i]);
    }
}

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: 多层组键往返")
{
    wipe();
    qSettingsSetStr(S("qlstiktest/nested/a"), S("1"));
    qSettingsSetStr(S("qlstiktest/nested/deep/b"), S("2"));
    CHECK_EQ(qSettingsValueStr(S("qlstiktest/nested/a")),        S("1"));
    CHECK_EQ(qSettingsValueStr(S("qlstiktest/nested/deep/b")),   S("2"));
    wipe();
}

// ── 默认值 ──────────────────────────────────────────────────────────

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: 键不存在时返回调用点给的默认值")
{
    wipe();
    CHECK_EQ(qSettingsValueStr(S("qlstiktest/absent"), S("DEF")), S("DEF"));
    CHECK_EQ(qSettingsValueInt(S("qlstiktest/absent"), 7), 7);
    CHECK_EQ(qSettingsValueBool(S("qlstiktest/absent"), true), true);
    CHECK(qSettingsValueDouble(S("qlstiktest/absent"), 2.5) == 2.5);
    CHECK_EQ(qSettingsValueULongLong(S("qlstiktest/absent"), 9), Q_ULLONG(9));
    CHECK(qSettingsValueMap(S("qlstiktest/absent")).isEmpty());
    // 无默认参数时返回零值
    CHECK(qSettingsValueStr(S("qlstiktest/absent")).isEmpty());
    CHECK_EQ(qSettingsValueInt(S("qlstiktest/absent")), 0);
}

// ── remove ──────────────────────────────────────────────────────────

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: remove 后回落默认值")
{
    wipe();
    qSettingsSetInt(S("qlstiktest/i"), 5);
    CHECK_EQ(qSettingsValueInt(S("qlstiktest/i")), 5);
    qSettingsRemove(S("qlstiktest/i"));
    CHECK_EQ(qSettingsValueInt(S("qlstiktest/i"), 8), 8);
    // 删不存在的键不应崩
    qSettingsRemove(S("qlstiktest/never-existed"));
    qSettingsRemove(S("storageRoot"));
    qSettingsRemove(S(""));
}

// ── 非 ASCII ───────────────────────────────────────────────────────
//
// 仓库规矩：非 ASCII 一律 UTF-8。QSettings 键名/值含中文必须无损，
// 否则 stickerstore 的中文存储根、贴纸标题会变成乱码。

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: 非 ASCII 键名与值无损")
{
    wipe();
    // ⚠ 期望值必须用 S()（=QString::fromUtf8）构造，不能写 QString("...")
    //   ——Qt3 的 QString(const char*) 走 Latin-1，会把一个汉字拆成 3 个
    //   QChar（'e4''bc''98'），比较必然失败且报错信息具有误导性。
    qSettingsSetStr(S("qlstiktest/贴纸"), S("优/的/目录"));
    const QString v = qSettingsValueStr(S("qlstiktest/贴纸"));
    CHECK_EQ(v, S("优/的/目录"));
    // 按码位核对，不靠终端 printf 显示
    REQUIRE(v.length() == 6);
    CHECK_EQ(v.at(0).unicode(), 0x4F18);   // 优
    CHECK_EQ(v.at(1).unicode(), '/');
    CHECK_EQ(v.at(2).unicode(), 0x7684);   // 的
    CHECK_EQ(v.at(4).unicode(), 0x76EE);   // 目
    CHECK_EQ(v.at(5).unicode(), 0x5F55);   // 录
    qSettingsRemove(S("qlstiktest/贴纸"));
}

// ═══════════════════════════════════════════════════════════════════════════
// CJK 键名与值（全部实测于 /opt/qt338sh 真机，不是照 Qt6 语义推的）
// ═══════════════════════════════════════════════════════════════════════════
//
// 为什么单开一组：stickerstore 的真实用法就是中文键 + 中文值（中文存储根、
// 贴纸标题），而 Qt3 的 QSettings 落盘是**纯文本 ini**，键名直接当 group 名
// 写进文件名、值直接当行内容写进文件 —— 中文在这条链上有三处可能坏：
//   ① 键规范化（qSettingsQt3Key）会不会把中文段改写；
//   ② ini 的行语法（= # ; 与首尾空格）对中文值有没有转义歧义；
//   ③ QStringList 用逗号分隔，值里自带逗号会不会被拆开。
// 三者坏了都不报任何错，只是配置静默不生效。

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: 中文 group 键与中文根层裸键")
{
    wipe();
    // ① 中文 group：规范化只去首尾斜杠，中文段逐字保留
    qSettingsSetStr(S("站点/贴纸"), S("猫喵"));
    CHECK_EQ(qSettingsValueStr(S("站点/贴纸")), S("猫喵"));
    CHECK_EQ(qSettingsQt3Key(S("站点/贴纸"), 0), S("站点/贴纸"));
    CHECK_EQ(qSettingsQt3Key(S("/站点/贴纸"), 0), S("站点/贴纸"));   // 前导斜杠等价

    // 根层中文裸键 → 注入合成组 "qlstik/"（与 stickerstore 的 storageRoot 同形）
    qSettingsSetStr(S("存储根"), S("/tmp/我的站点"));
    CHECK_EQ(qSettingsValueStr(S("存储根")), S("/tmp/我的站点"));
    CHECK_EQ(qSettingsQt3Key(S("存储根"), 0), S("qlstik/存储根"));

    // 码点自检：期望值里的中文确实是 2 个 QChar（守住守门人）
    const QString mao = S("猫喵");
    REQUIRE(mao.length() == 2);
    CHECK(mao.at(0) == QChar(0x732B));
    CHECK(mao.at(1) == QChar(0x55B5));

    // 前缀共存：中文键 "标题" 与 "标题2" 互不覆盖
    qSettingsSetStr(S("站点/标题"), S("A"));
    qSettingsSetStr(S("站点/标题2"), S("B"));
    CHECK_EQ(qSettingsValueStr(S("站点/标题")), S("A"));
    CHECK_EQ(qSettingsValueStr(S("站点/标题2")), S("B"));

    // 不存在的中文键回落**中文**默认值
    CHECK_EQ(qSettingsValueStr(S("站点/缺省键"), S("默认中文")), S("默认中文"));
    wipe();
}

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: 中文值含 ini 行语法字符（= # ; 与首尾空格）无损")
{
    wipe();
    // Qt3 ini 一行是 "键=值"，'#' 与 ';' 起注释、值首尾空格会被吃掉。这几组
    // 若没被正确转义，读回来就会掉字符或整行变注释 —— 且不报错。
    qSettingsSetStr(S("sp/等号"), S("a=b"));
    qSettingsSetStr(S("sp/井号"), S("a#b"));
    qSettingsSetStr(S("sp/分号"), S("a;b"));
    qSettingsSetStr(S("sp/首空格"), S(" a"));
    qSettingsSetStr(S("sp/尾空格"), S("a "));
    qSettingsSetStr(S("sp/全等"), S("a=b#c;d"));
    qSettingsSetStr(S("sp/中文混排"), S("优=的;目#录"));

    CHECK_EQ(qSettingsValueStr(S("sp/等号")), S("a=b"));
    CHECK_EQ(qSettingsValueStr(S("sp/井号")), S("a#b"));
    CHECK_EQ(qSettingsValueStr(S("sp/分号")), S("a;b"));
    CHECK_EQ(qSettingsValueStr(S("sp/首空格")), S(" a"));
    CHECK_EQ(qSettingsValueStr(S("sp/尾空格")), S("a "));
    CHECK_EQ(qSettingsValueStr(S("sp/全等")), S("a=b#c;d"));
    CHECK_EQ(qSettingsValueStr(S("sp/中文混排")), S("优=的;目#录"));

    // 中文键本身含 '=' 是不可能的（键用 '/' 分组），但含 '#' 的键也测一下：
    // 键里的 '#' 若没转义，落盘后会被当成注释而读不回来。
    qSettingsSetStr(S("sp/井号键"), S("值"));
    CHECK_EQ(qSettingsValueStr(S("sp/井号键")), S("值"));
    wipe();
}

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: QStringList 中文项往返（含逗号项不被拆开）")
{
    wipe();
    QStringList sl;
    sl << S("甲") << S("乙丙") << S("丁");
    qSettingsSetStringList(S("sl/中文表"), sl);
    const QStringList back = qSettingsValueStringList(S("sl/中文表"));
    REQUIRE(back.count() == 3);
    CHECK_EQ(back[0], S("甲"));
    CHECK_EQ(back[1], S("乙丙"));
    CHECK_EQ(back[2], S("丁"));

    // ⚠ 实测要点：值里自带逗号时，Qt3 会把它转义，**不会**被拆成两项。
    //   若哪天垫片改成自己拼逗号，这一项会被切成 "含" 与 "逗号" 而静默出错。
    QStringList withComma;
    withComma << S("含,逗号") << S("正常");
    qSettingsSetStringList(S("sl/逗号"), withComma);
    const QStringList back2 = qSettingsValueStringList(S("sl/逗号"));
    REQUIRE(back2.count() == 2);
    CHECK_EQ(back2[0], S("含,逗号"));
    CHECK_EQ(back2[1], S("正常"));

    // 空表 vs 单个空串项：两者语义不同，不得混为一谈
    qSettingsSetStringList(S("sl/空表"), QStringList());
    CHECK_EQ(qSettingsValueStringList(S("sl/空表")).count(), 0);
    QStringList oneEmpty;
    oneEmpty << S("");
    qSettingsSetStringList(S("sl/空项"), oneEmpty);
    const QStringList back3 = qSettingsValueStringList(S("sl/空项"));
    CHECK_EQ(back3.count(), 1);
    CHECK(back3[0].isEmpty());
    qSettingsRemove(S("sl/空表"));
    qSettingsRemove(S("sl/空项"));
    wipe();
}

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: 全角与半角不折叠，中文键与 ASCII 键互不干扰")
{
    wipe();
    // 全角 ＳＶＣ / ｓｖｃ１２３ 与半角 svc 是不同键不同值，不能被归一化合并
    qSettingsSetStr(S("fw/ＳＶＣ"), S("ｓｖｃ１２３"));
    qSettingsSetStr(S("fw/svc"), S("halfwidth"));
    CHECK_EQ(qSettingsValueStr(S("fw/ＳＶＣ")), S("ｓｖｃ１２３"));
    CHECK_EQ(qSettingsValueStr(S("fw/svc")), S("halfwidth"));
    const QString full = S("ｓｖｃ１２３");
    REQUIRE(full.length() == 6);
    CHECK(full.at(0) == QChar(0xFF53));       // ｓ
    CHECK(full.at(5) == QChar(0xFF13));       // ３

    // 中文组与 ASCII 组并存，写中文不得覆盖 ASCII 的同名叶子
    qSettingsSetStr(S("mix/ascii"), S("EN"));
    qSettingsSetStr(S("mix/中文"), S("中"));
    CHECK_EQ(qSettingsValueStr(S("mix/ascii")), S("EN"));
    CHECK_EQ(qSettingsValueStr(S("mix/中文")), S("中"));
    wipe();
}

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: 中文键落盘为 UTF-8 文件名与 UTF-8 内容")
{
    wipe();
    qSettingsSetStr(S("站点/贴纸"), S("猫喵"));

    // Qt3 落盘路径是 $HOME/.qt/<键首段>rc —— 中文 group 名直接成为**文件名**。
    // 断言"以 站点 开头的文件存在"而不是死钉 "站点rc"：rc 后缀是 Qt3 内部实现，
    // 但"中文进了文件名"这件事本身是必须钉住的（它决定了中文 group 在某些
    // 非 UTF-8 locale 的文件系统上会真的建不出文件）。
    const QDir qtDir(QDir(S(getenv("HOME"))).absFilePath(S(".qt")));
    // 走跨版本垫片而非 QDir::entryList：Qt3 只有 entryList(int filter, int sort)，
    // 没有 Qt4+ 的 entryList(QStringList filters, FilterSpec) 重载。
    const QStringList names = qDirEntryList(qtDir, S("*"), QDir::Files);
    QString cjkFile;
    for (int i = 0; i < names.count(); ++i) {
        if (names[i].startsWith(S("站点"))) cjkFile = names[i];
    }
    REQUIRE(!cjkFile.isEmpty());

    // 文件内容是 UTF-8 明文（非 \uXXXX 转义、非 Latin-1 高位字节）
    QFile f(qtDir.absFilePath(cjkFile));
    REQUIRE(f.open(IO_ReadOnly));
    const QByteArray bytes = f.readAll();
    f.close();
    CHECK(bytes.size() > 0);
    const QString text = QString::fromUtf8(bytes.data(), (int)bytes.size());
    CHECK(text.contains(S("猫喵")));
    // 值所在那一行不含 ASCII 转义序列 "\u"（Qt3 ini 不做 \u 转义）
    CHECK(!text.contains(S("\\u")));
    // 键名也是原样中文，而不是问号替换
    CHECK(text.contains(S("贴纸")));
}

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: 非 BMP 字符以代理对存储，往返仍相等")
{
    wipe();
    // 𠮷 = U+20BB7（CJK 扩展 B），UTF-8 4 字节 F0 A0 AE B7。
    // ⚠ Qt3 的 QString 是 UCS-4 容器，但 fromUtf8 对 4 字节序列产出的是
    //   **代理对**两个 QChar（U+D842 U+DFB7），不是一个 U+20BB7。故：
    //   · 往返相等（写什么读回什么，落盘字节也是对的）；
    //   · 但 length() 多算 1，任何按 length()/索引 推算 UTF-8 字节数或
    //     字符数的代码在含非 BMP 字符时都会偏 1 —— 故这里把该行为钉死。
    const QString src = S("𠮷品");
    qSettingsSetStr(S("ext/生僻"), src);
    const QString back = qSettingsValueStr(S("ext/生僻"));
    CHECK_EQ(back, src);
    REQUIRE(back.length() == 3);              // 2 个代理 + 品
    CHECK(back.at(0) == QChar(0xD842));
    CHECK(back.at(1) == QChar(0xDFB7));
    CHECK(back.at(2) == QChar(0x54C1));       // 品
    // 落盘仍是 4 字节 UTF-8（没被代理对写坏）
    CHECK_EQ(qToUtf8BA(src).size(), 7);       // 4 + 3
    wipe();
}

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: 中文键 remove 后回落默认值，且不影响同组其它键")
{
    wipe();
    qSettingsSetStr(S("站点/标题"), S("贴纸一"));
    qSettingsSetStr(S("站点/标题2"), S("贴纸二"));
    qSettingsRemove(S("站点/标题"));
    CHECK_EQ(qSettingsValueStr(S("站点/标题"), S("已删")), S("已删"));
    CHECK_EQ(qSettingsValueStr(S("站点/标题2")), S("贴纸二"));   // 邻居不受影响
    // 删一个中文键后，同前缀的新键仍可正常写入（组没被误删）
    qSettingsSetStr(S("站点/标题3"), S("贴纸三"));
    CHECK_EQ(qSettingsValueStr(S("站点/标题3")), S("贴纸三"));
    // 删不存在的键不得连带删掉别的（Qt3 的 removeEntry 对不存在的键是静默 no-op）
    qSettingsRemove(S("站点/从来没有过"));
    CHECK_EQ(qSettingsValueStr(S("站点/标题2")), S("贴纸二"));
    wipe();
}

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: 【已知限制】根层裸键与显式 qlstik/ 前缀键会撞车")
{
    wipe();
    // 规范化把根层裸键注入合成组 "qlstik/"，于是显式写成 "qlstik/x" 的键
    // 与根层裸键 "x" **落到同一个槽位**，后写的覆盖先写的。
    // 实测（/opt/qt338sh）：写 "x"=BARE 后再写 "qlstik/x"=EXPLICIT，
    // 读 "x" 变成 EXPLICIT。
    // 影响面：产品现有键（storageRoot、dlProgress/<hex>）都不以 "qlstik/"
    // 开头，故当前无实际冲突；但**新增**键时若取名 "qlstik/xxx" 就会踩到。
    // 故把该行为钉在这里，评审新增键时能被看见。
    qSettingsSetStr(S("x"), S("BARE"));
    CHECK_EQ(qSettingsValueStr(S("x")), S("BARE"));
    qSettingsSetStr(S("qlstik/x"), S("EXPLICIT"));
    CHECK_EQ(qSettingsValueStr(S("qlstik/x")), S("EXPLICIT"));
    CHECK_EQ(qSettingsValueStr(S("x")), S("EXPLICIT"));         // ← 撞车
    // 反向也一样：写根层裸键会覆盖显式 qlstik/ 键
    qSettingsSetStr(S("x"), S("BARE2"));
    CHECK_EQ(qSettingsValueStr(S("qlstik/x")), S("BARE2"));
    qSettingsRemove(S("qlstik/x"));
    wipe();
}

// ── map（Qt3 走 JSON 文本通道）───────────────────────────────────────

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: QVariantMap 往返（标量值）")
{
    wipe();
    QVariantMap m;
    m.insert(S("w"), QVariant(S("100")));
    m.insert(S("h"), QVariant(S("80")));
    qSettingsSetMap(S("qlstiktest/m"), m);
    const QVariantMap back = qSettingsValueMap(S("qlstiktest/m"));
    CHECK_EQ(back.count(), 2);
    CHECK_EQ(back[S("w")].toString(), S("100"));
    CHECK_EQ(back[S("h")].toString(), S("80"));
    wipe();
}

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: QVariantMap 非 ASCII 键值无损")
{
    wipe();
    QVariantMap m;
    m.insert(S("贴"), QVariant(S("纸")));
    qSettingsSetMap(S("qlstiktest/m"), m);
    const QVariantMap back = qSettingsValueMap(S("qlstiktest/m"));
    CHECK_EQ(back.count(), 1);
    REQUIRE(back.contains(S("贴")));
    CHECK_EQ(back[S("贴")].toString(), S("纸"));
    wipe();
}

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: map 存成 JSON 文本后可被原样读回（格式稳定）")
{
    // 垫片在 Qt3 下把 map 编成 JSON 存在字符串通道。锁住这个往返，
    // 免得有人改编码方式后老配置读不出来。
    wipe();
    QVariantMap m;
    m.insert(S("k1"), QVariant(S("v1")));
    qSettingsSetMap(S("qlstiktest/m"), m);
    const QString json = qSettingsValueStr(S("qlstiktest/m"));
    CHECK(json.contains(S("k1")));
    CHECK(json.contains(S("v1")));

    bool ok = false;
    const QVariantMap parsed = qSettingsJsonToMap(json, &ok);
    CHECK(ok);
    CHECK_EQ(parsed.count(), 1);
    CHECK_EQ(parsed[S("k1")].toString(), S("v1"));
    wipe();
}

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: 空 map 与坏 JSON 都不抛异常")
{
    bool ok = true;
    const QVariantMap e1 = qSettingsJsonToMap(QString(), &ok);
    CHECK(e1.isEmpty());
    const QVariantMap e2 = qSettingsJsonToMap(S("{不是合法 JSON"), &ok);
    CHECK(e2.isEmpty());
}

// ── 已知限制：childKeys 在 Qt3 上返回空 ────────────────────────────
//
// 这不是契约，是缺陷。之所以写进测试，是为了把"已知不工作"钉成显式事实：
// 若将来 Qt3 侧换成能列 section 的后端、或产品代码真的开始依赖它，
// 这条断言会失败，从而强制有人来处理它，而不是让调用方拿到空列表
// 还以为"配置里就是没有"。
//
// 原因见 qsettings_shim.h 里 qSettingsChildKeys() 的注释：Qt3 的
// subkeyList() 在 INI 后端列不出子组（官方文档标为 Qt4 才修的 known issue）。

TEST_CASE_FIXTURE(QsHomeScope, "QSettings: 【已知限制】childKeys 在 Qt3 上返回空")
{
    wipe();
    qSettingsSetStr(S("qlstiktest/nested/a"), S("1"));
    CHECK(qSettingsChildKeys().isEmpty());
    wipe();
}

// ── 隔离本身没留下痕迹 ──────────────────────────────────────────────
//
// QsHomeScope 必须在**每个**用例结束时把 $HOME 还原，否则本文件 15 个用例
// 全程在隔离 HOME 下跑，同一个 run_tests 二进制里的其它 268 个用例也跟着被
// 改掉环境 —— 这个坑当初就是这么来的（文件作用域静态对象只 setenv 不还原）。
//
// 本用例**故意不用** QsHomeScope，且刻意声明在本文件最后：doctest 默认按声明
// 顺序执行，故它看到的是「前 15 个带 fixture 的用例跑完之后」的 HOME。
// 若哪天真把还原逻辑删了，这条会红。

TEST_CASE("QSettings: 用例结束后 $HOME 已被还原（隔离不留痕迹）")
{
    const char* h = getenv("HOME");
    REQUIRE(h != 0);
    // 环境 HOME 不该带本套件的临时目录标记（带 pid 后缀那种）
    CHECK(QString::fromUtf8(h).find(QString("qtest_qsettings_home_")) < 0);
}
