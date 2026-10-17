// qlstik/test/test_config.cpp —— Config 契约（Qt3 单端，零网络）
//
// 被测对象：qlstik/src/config.cpp
//   现后端为 QSettings34，读写 ~/.config/fedlet/anystik.conf —— 与 anystik（Qt6
//   QSettings）**共用同一份**。故本套件同时充当"两程序互读"的契约测试。
//   value(key, def)  缺文件/键不存在 → 回退 def
//   setValue(k, v)   读-改-写，保留其它键（含未触碰的 raw 值，如 @Variant）
//   uiLang/styleId/darkMode  三个便捷读取，带默认值
//
// ★ 隔离手法：重定向 $HOME，并**清掉 $XDG_CONFIG_HOME**。
//   QSettings34::userConfigDir() 优先看 $XDG_CONFIG_HOME（绝对路径时），
//   若不清掉，用例会写进用户真实配置目录 —— 故这里必须 unset。守卫析构还原。
//
// ★ 与 stikcommon 相同的 Qt3 雷：doctest 2.4.11 格式化 **null QString** 会 SIGSEGV，
//   故凡期望为空的，一律断言 .isEmpty() / .length()，不把 QString 送进 == 链。

#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qstring.h>
#include <qstringlist.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdlib.h>
#include <string>
#include <unistd.h>

#include "doctest/doctest.h"

#include "config.h"

// Qt3 的 QDir 无 tempPath()（Qt 4.2 才加）。按 POSIX 约定取 TMPDIR，未设则 /tmp。
static QString tempRootBase()
{
    const char* const td = ::getenv("TMPDIR");
    if (td && *td) {
        return QString(QCString(td));
    }
    return QString::fromLatin1("/tmp");
}

// ── 脚手架：每次一个独立 $HOME，析构时还原环境并清树 ────────────────
class FakeHome
{
public:
    FakeHome()
    {
        const char* old = ::getenv("HOME");
        m_saved = (old && *old) ? QString(QCString(old)) : QString();
        m_hadOld = (old != 0);

        // ⚠ QSettings34 优先看 XDG_CONFIG_HOME；不清掉会写到真实目录
        const char* xdg = ::getenv("XDG_CONFIG_HOME");
        m_xdgSaved = (xdg && *xdg) ? QString(QCString(xdg)) : QString();
        m_xdgHad = (xdg != 0);
        ::unsetenv("XDG_CONFIG_HOME");

        m_root = tempRootBase() + QString("/qlstik-cfg-")
                 + QString::number((long)getpid()) + QString("-")
                 + QString::number(++s_seq);
        removeTree(m_root);
        ::setenv("HOME", m_root.utf8(), 1);
    }

    ~FakeHome()
    {
        if (m_hadOld) {
            ::setenv("HOME", m_saved.utf8(), 1);
        } else {
            ::unsetenv("HOME");
        }
        if (m_xdgHad) {
            ::setenv("XDG_CONFIG_HOME", m_xdgSaved.utf8(), 1);
        }
        removeTree(m_root);
    }

    const QString& rootForCheck() const { return m_root; }

    // 直接往配置目录塞原始 INI 内容，构造坏文件 / 手工 @Variant 场景
    void writeRawConfig(const std::string& content) const
    {
        const QString dir = Config::configDir();
        QDir d(dir);
        if (!d.exists()) {
            CHECK(qMkdir(dir));
        }
        std::ofstream ofs(std::string(Config::configFilePath().utf8()).c_str());
        CHECK(ofs.is_open());
        ofs << content;
    }

    std::string readRawConfig() const
    {
        std::ifstream ifs(std::string(Config::configFilePath().utf8()).c_str());
        if (!ifs.is_open()) {
            return std::string();
        }
        std::stringstream ss;
        ss << ifs.rdbuf();
        return ss.str();
    }

    static int countOccurrences(const std::string& hay, const std::string& needle)
    {
        int n = 0;
        size_t p = 0;
        while ((p = hay.find(needle, p)) != std::string::npos) {
            ++n;
            p += needle.size();
        }
        return n;
    }

    static void removeTree(const QString& path)
    {
        QDir d(path);
        if (!d.exists()) {
            return;
        }
        // ⚠ Qt3 的 entryList() 会把 "." 和 ".." 也列出来，必须显式跳过。
        const QStringList kids = d.entryList(QDir::All | QDir::Hidden | QDir::System);
        for (int i = 0; i < kids.size(); ++i) {
            const QString name = kids[i];
            if (name == "." || name == "..") {
                continue;
            }
            const QString p = path + QString("/") + name;
            if (QFileInfo(p).isDir() && !QFileInfo(p).isSymLink()) {
                removeTree(p);
            } else {
                QFile::remove(p);
            }
        }
        QDir().rmdir(path);
    }

private:
    static int s_seq;
    QString m_root;
    QString m_saved;
    QString m_xdgSaved;
    bool m_hadOld;
    bool m_xdgHad;
};

int FakeHome::s_seq = 0;

// ── 路径：与 anystik 共用 ──────────────────────────────────────────

TEST_CASE("Config: 路径固定在 $HOME/.config/fedlet/anystik.conf")
{
    FakeHome h;                              // 构造时已把 $HOME 指向临时目录
    const QString dir = Config::configDir();
    const QString file = Config::configFilePath();
    // 必须落在刚设的临时 HOME 里 —— 同时验证"重定向 $HOME 有效"
    CHECK(dir.startsWith(h.rootForCheck()));
    CHECK(dir.endsWith(QString("/.config/fedlet")));
    CHECK(file == dir + QString("/anystik.conf"));
    CHECK(file.endsWith(QString("/.config/fedlet/anystik.conf")));
    // 未创建任何东西前目录不该存在
    CHECK(QDir(Config::configDir()).exists() == false);
    CHECK(QFile(Config::configFilePath()).exists() == false);
}

// ── 默认值回退 ─────────────────────────────────────────────────────

TEST_CASE("Config: 无配置文件时全部走默认值")
{
    FakeHome h;
    CHECK(Config::uiLang() == QString("zh-CN"));
    CHECK(Config::styleId() == QString("qtFusion"));
    CHECK(Config::darkMode() == true);          // 默认 true（与 anystik 一致）
    // 裸 value：无 def 且键不存在 → 空串
    CHECK(Config::value(QString("nosuchkey")).isEmpty());
    CHECK(Config::value(QString("nosuchkey"), QString("fallback"))
          == QString("fallback"));
    CHECK(Config::value(QString("nosuchkey"), QString("")).isEmpty());
}

// ── setValue 往返 ──────────────────────────────────────────────────

TEST_CASE("Config: setValue 写入后可读回，并自动建目录")
{
    FakeHome h;
    CHECK(Config::setValue(QString("style"), QString("material")) == true);
    CHECK(Config::styleId() == QString("material"));

    CHECK(QDir(Config::configDir()).exists() == true);
    CHECK(QFile(Config::configFilePath()).exists() == true);

    CHECK(Config::value(QString("style")) == QString("material"));
}

TEST_CASE("Config: setValue 保留其它键（读-改-写）")
{
    FakeHome h;
    CHECK(Config::setUiLang(QString("zh-TW")));
    CHECK(Config::setValue(QString("style"), QString("material")));
    CHECK(Config::setValue(QString("custom"), QString("keepme")));

    CHECK(Config::uiLang() == QString("zh-TW"));
    CHECK(Config::styleId() == QString("material"));
    CHECK(Config::value(QString("custom")) == QString("keepme"));

    const std::string raw = h.readRawConfig();
    CHECK(FakeHome::countOccurrences(raw, "language=") == 1);
    CHECK(FakeHome::countOccurrences(raw, "style=") == 1);
    CHECK(FakeHome::countOccurrences(raw, "custom=") == 1);
}

TEST_CASE("Config: 覆盖已有键不产生重复键")
{
    FakeHome h;
    CHECK(Config::setValue(QString("style"), QString("first")));
    CHECK(Config::setValue(QString("style"), QString("second")));
    CHECK(Config::setValue(QString("style"), QString("third")));
    CHECK(Config::styleId() == QString("third"));
    const std::string raw = h.readRawConfig();
    CHECK(FakeHome::countOccurrences(raw, "style=") == 1);
}

// ── darkMode（bool，跨 anystik 互读）───────────────────────────────

TEST_CASE("Config: darkMode 按 bool 存取，与 anystik 互通")
{
    FakeHome h;
    CHECK(Config::darkMode() == true);          // 默认 true

    CHECK(Config::setDarkMode(false));
    CHECK(Config::darkMode() == false);
    CHECK(h.readRawConfig().find("darkMode=false") != std::string::npos);

    CHECK(Config::setDarkMode(true));
    CHECK(Config::darkMode() == true);
    CHECK(h.readRawConfig().find("darkMode=true") != std::string::npos);

    // 模拟 anystik 写入 → qlstik 读到
    h.writeRawConfig("[General]\ndarkMode=false\n");
    CHECK(Config::darkMode() == false);
    h.writeRawConfig("[General]\ndarkMode=true\n");
    CHECK(Config::darkMode() == true);
}

// ── language 归一（en ↔ en-US）─────────────────────────────────────

TEST_CASE("Config: language 与 anystik 归一（en ↔ en-US）")
{
    FakeHome h;
    // anystik 存 "en" → qlstik 读成 "en-US"
    h.writeRawConfig("[General]\nlanguage=en\n");
    CHECK(Config::uiLang() == QString("en-US"));

    // qlstik 写 "en-US" → 落盘仍是 "en"
    CHECK(Config::setUiLang(QString("en-US")));
    CHECK(FakeHome::countOccurrences(h.readRawConfig(), "language=en\n") == 1);
    CHECK(Config::uiLang() == QString("en-US"));

    // zh-CN / zh-TW 原样
    CHECK(Config::setUiLang(QString("zh-TW")));
    CHECK(Config::uiLang() == QString("zh-TW"));
    CHECK(h.readRawConfig().find("language=zh-TW") != std::string::npos);
}

// ── 与 anystik 的 @Variant 值共存，不被改坏 ────────────────────────

TEST_CASE("Config: 改 General 键不破坏其它段的 @Variant 值")
{
    FakeHome h;
    // 该串由 Qt6 QSettings 实际写出（QVariantMap{k=7,s="v"}）
    const char* kRaw =
        "[General]\nlanguage=zh-CN\n"
        "[downloadedPackMeta]\n"
        "pack_x=@Variant(\\0\\0\\0\\b\\0\\0\\0\\x2\\0\\0\\0\\x2\\0k\\0\\0\\0"
        "\\x2\\0\\0\\0\\a\\0\\0\\0\\x2\\0s\\0\\0\\0\\n\\0\\0\\0\\x2\\0v)\n";
    h.writeRawConfig(kRaw);

    CHECK(Config::setValue(QString("style"), QString("github")) == true);

    const std::string raw = h.readRawConfig();
    CHECK(raw.find("@Variant(") != std::string::npos);
    CHECK(FakeHome::countOccurrences(raw, "pack_x=") == 1);
    CHECK(FakeHome::countOccurrences(raw, "language=") == 1);
    // 原值字节未被改写
    CHECK(raw.find("\\0k\\0\\0\\0") != std::string::npos);
}

// ── 坏文件 / 空文件 ────────────────────────────────────────────────

TEST_CASE("Config: 坏文件与空文件都回退默认，不崩")
{
    FakeHome h;
    h.writeRawConfig("this is not ini at all {{{");
    CHECK(Config::uiLang() == QString("zh-CN"));
    CHECK(Config::styleId() == QString("qtFusion"));
    CHECK(Config::darkMode() == true);

    h.writeRawConfig("");
    CHECK(Config::uiLang() == QString("zh-CN"));
    CHECK(Config::value(QString("nosuchkey"), QString("fb")) == QString("fb"));
}

// ── UTF-8 ──────────────────────────────────────────────────────────

TEST_CASE("Config: UTF-8 键值往返")
{
    FakeHome h;
    CHECK(Config::setUiLang(QString::fromUtf8("中文")));   // 非常规值也原样往返
    const QString got = Config::uiLang();
    CHECK(got == QString::fromUtf8("中文"));
    CHECK(got.length() == 2);                 // 2 个汉字，不是 6 个字节
    CHECK((int)got.at(0).unicode() == 0x4E2D); // 中
    CHECK((int)got.at(1).unicode() == 0x6587); // 文

    CHECK(Config::setValue(QString::fromUtf8("键名"), QString::fromUtf8("值")));
    CHECK(Config::value(QString::fromUtf8("键名")) == QString::fromUtf8("值"));
}
