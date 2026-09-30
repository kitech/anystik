// qlstik/test/test_config.cpp —— Config 契约（Qt3 单端，零网络）
//
// 被测对象：qlstik/src/config.cpp（cJSON 读写 ~/.config/qlstik/config.json）
//   value(key, def)  缺文件/坏文件/键不存在/值类型不对 → 一律回退 def
//   setValue(k, v)   读-改-写，保留其它键；先删旧键再写（cJSON 的 add 是追加）
//   uiLang/styleId/darkMode  三个便捷读取，带默认值
//
// ★ 隔离手法：重定向 $HOME，绝不碰用户真实配置
//
//   Config::configDir() 走 QDir::homeDirPath()。实测（/tmp/opencode/home6.cpp）
//   它**每次调用都重读 $HOME、没有静态缓存**，所以在用例里 setenv("HOME", 临时)
//   就能把整套读写完全圈进 /tmp。这是本文件不污染 ~/.config 的依据；
//   守卫析构时把 $HOME 还原，并递归清掉临时树。
//
// ★ 与 stikcommon 相同的 Qt3 雷：doctest 2.4.11 格式化 **null QString** 会 SIGSEGV，
//   而本套件大量断言"键不存在 → 回退"这种会拿到 null QString 的路径。
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

// Qt3 的 QDir 无 tempPath()（实测 qdir.h 无该声明，Qt 4.2 才加）。
// 按 POSIX 约定取 TMPDIR，未设则 /tmp —— 与 stikcommon/test_qmkdir.cpp 同一约定。
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
        // 同 pid 下多个用例靠自增序号区分
        m_root = tempRootBase() + QString("/qlstik-cfg-")
                 + QString::number((long)getpid()) + QString("-")
                 + QString::number(++s_seq);
        removeTree(m_root);
        setenv("HOME", m_root.utf8(), 1);
    }

    ~FakeHome()
    {
        if (m_hadOld) {
            setenv("HOME", m_saved.utf8(), 1);
        } else {
            unsetenv("HOME");
        }
        removeTree(m_root);
    }

    // 临时 HOME 根，供用例确认 Config 确实落在重定向后的目录里
    const QString& rootForCheck() const { return m_root; }

    // 直接往配置目录塞原始内容，构造坏文件 / 手工 JSON 场景
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
        // ⚠ Qt3 的 entryList() 会把 "." 和 ".." 也列出来，必须显式跳过，
        //   否则 "." 被判成目录而无限递归（stikcommon/test_qmkdir.cpp 栽过）。
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
    bool m_hadOld;
};

int FakeHome::s_seq = 0;

// ── 路径 ───────────────────────────────────────────────────────────

TEST_CASE("Config: 路径固定在 $HOME/.config/qlstik 下")
{
    FakeHome h;                              // 构造时已把 $HOME 指向临时目录
    const QString dir = Config::configDir();
    const QString file = Config::configFilePath();
    // 必须落在刚设的临时 HOME 里 —— 这条同时验证了"重定向 $HOME 有效"
    CHECK(dir.startsWith(h.rootForCheck()));
    CHECK(dir.endsWith(QString("/.config/qlstik")));
    CHECK(file == dir + QString("/config.json"));
    CHECK(file.endsWith(QString("/.config/qlstik/config.json")));
    // 未创建任何东西前目录不该存在
    CHECK(QDir(Config::configDir()).exists() == false);
    CHECK(QFile(Config::configFilePath()).exists() == false);
}

// ── 默认值回退 ─────────────────────────────────────────────────────

TEST_CASE("Config: 无配置文件时全部走默认值")
{
    FakeHome h;
    // 三个便捷读取的默认值（config.cpp 里写死的）
    CHECK(Config::uiLang() == QString("zh-CN"));
    CHECK(Config::styleId() == QString("qtFusion"));
    // dark 默认 "true" → darkMode() 为 true
    CHECK(Config::darkMode() == true);
    // 裸 value：无 def 且键不存在 → 返回 null QString
    CHECK(Config::value(QString("nosuchkey")).isEmpty());
    // 带 def → 返回 def
    CHECK(Config::value(QString("nosuchkey"), QString("fallback"))
          == QString("fallback"));
    // 默认值也为空串时同样是空
    CHECK(Config::value(QString("nosuchkey"), QString("")).isEmpty());
}

// ── setValue 往返 ──────────────────────────────────────────────────

TEST_CASE("Config: setValue 写入后可读回")
{
    FakeHome h;
    CHECK(Config::setValue(QString("uilang"), QString("en-US")) == true);
    CHECK(Config::uiLang() == QString("en-US"));

    // 目录与文件被自动创建
    CHECK(QDir(Config::configDir()).exists() == true);
    CHECK(QFile(Config::configFilePath()).exists() == true);

    // 裸 value 也能读到同一个键
    CHECK(Config::value(QString("uilang")) == QString("en-US"));
    // 其它键不受影响，仍走默认
    CHECK(Config::styleId() == QString("qtFusion"));
}

TEST_CASE("Config: setValue 保留其它键（读-改-写）")
{
    FakeHome h;
    CHECK(Config::setValue(QString("uilang"), QString("zh-TW")));
    CHECK(Config::setValue(QString("style"), QString("material")));
    CHECK(Config::setValue(QString("custom"), QString("keepme")));

    // 三个都在
    CHECK(Config::uiLang() == QString("zh-TW"));
    CHECK(Config::styleId() == QString("material"));
    CHECK(Config::value(QString("custom")) == QString("keepme"));

    // 原始 JSON 里三个键都存在
    const std::string raw = h.readRawConfig();
    CHECK(FakeHome::countOccurrences(raw, "\"uilang\"") == 1);
    CHECK(FakeHome::countOccurrences(raw, "\"style\"") == 1);
    CHECK(FakeHome::countOccurrences(raw, "\"custom\"") == 1);
}

TEST_CASE("Config: 覆盖已有键不产生重复键")
{
    FakeHome h;
    CHECK(Config::setValue(QString("uilang"), QString("first")));
    CHECK(Config::setValue(QString("uilang"), QString("second")));
    CHECK(Config::setValue(QString("uilang"), QString("third")));

    // ⚠ cJSON_AddStringToObject 是**追加**不是替换。若 setValue 不先
    //   cJSON_DeleteItemFromObject，文件里会有 3 个 "uilang"，
    //   而 cJSON_GetObjectItem 取**首个**匹配 → 回读会拿到 "first"（旧值）。
    //   这里断言拿到最后一次的值，且原始 JSON 里键只出现一次。
    CHECK(Config::uiLang() == QString("third"));
    const std::string raw = h.readRawConfig();
    CHECK(FakeHome::countOccurrences(raw, "\"uilang\"") == 1);
}

// ── darkMode 语义 ──────────────────────────────────────────────────

TEST_CASE("Config: darkMode 只认字符串 \"true\"")
{
    FakeHome h;
    // 默认 true
    CHECK(Config::darkMode() == true);

    CHECK(Config::setValue(QString("dark"), QString("false")));
    CHECK(Config::darkMode() == false);

    CHECK(Config::setValue(QString("dark"), QString("true")));
    CHECK(Config::darkMode() == true);

    // 其它字符串都不是 true —— 实现是 value("dark","true") == "true"，严格相等
    CHECK(Config::setValue(QString("dark"), QString("yes")));
    CHECK(Config::darkMode() == false);
    CHECK(Config::setValue(QString("dark"), QString("TRUE")));
    CHECK(Config::darkMode() == false);        // 大写不算
    CHECK(Config::setValue(QString("dark"), QString("1")));
    CHECK(Config::darkMode() == false);
    // 回到默认
    CHECK(Config::setValue(QString("dark"), QString("")));
    CHECK(Config::darkMode() == false);        // "" != "true"
}

// ── 坏文件 / 异常 JSON ─────────────────────────────────────────────

TEST_CASE("Config: 坏文件与空文件都回退默认，不崩")
{
    FakeHome h;
    // 完全不是 JSON
    h.writeRawConfig("this is not json at all {{{");
    CHECK(Config::uiLang() == QString("zh-CN"));
    CHECK(Config::styleId() == QString("qtFusion"));
    CHECK(Config::darkMode() == true);

    // 空文件
    h.writeRawConfig("");
    CHECK(Config::uiLang() == QString("zh-CN"));
    CHECK(Config::value(QString("uilang"), QString("fb")) == QString("fb"));
}

TEST_CASE("Config: JSON 值类型不对时回退默认")
{
    FakeHome h;
    // 数字：既不是字符串也不是布尔 → 回退 def
    h.writeRawConfig("{\"uilang\": 123}");
    CHECK(Config::uiLang() == QString("zh-CN"));
    // 但带自定义 def 时返回自定义 def
    CHECK(Config::value(QString("uilang"), QString("fb")) == QString("fb"));

    // null
    h.writeRawConfig("{\"uilang\": null}");
    CHECK(Config::uiLang() == QString("zh-CN"));

    // 对象 / 数组
    h.writeRawConfig("{\"uilang\": {\"a\": 1}}");
    CHECK(Config::uiLang() == QString("zh-CN"));
    h.writeRawConfig("{\"uilang\": [1,2]}");
    CHECK(Config::uiLang() == QString("zh-CN"));

    // 根不是对象（顶层数组）→ 取不到键，回退
    h.writeRawConfig("[1, 2, 3]");
    CHECK(Config::uiLang() == QString("zh-CN"));
}

TEST_CASE("Config: JSON 布尔值被转成 \"true\"/\"false\" 字符串")
{
    FakeHome h;
    // cJSON_IsTrue/cJSON_IsFalse 分支（config.cpp 的 value 里显式处理了）
    h.writeRawConfig("{\"dark\": true}");
    CHECK(Config::value(QString("dark")) == QString("true"));
    CHECK(Config::darkMode() == true);

    h.writeRawConfig("{\"dark\": false}");
    CHECK(Config::value(QString("dark")) == QString("false"));
    CHECK(Config::darkMode() == false);

    // 布尔值带 def 时：类型命中就覆盖 def
    h.writeRawConfig("{\"dark\": false}");
    CHECK(Config::value(QString("dark"), QString("true")) == QString("false"));
}

// ── UTF-8 ──────────────────────────────────────────────────────────

TEST_CASE("Config: UTF-8 值往返（qToUtf8/qFromUtf8）")
{
    FakeHome h;
    // 中文值必须按 UTF-8 字节往返，不能被 Latin-1 逐字节解释成等长乱码
    CHECK(Config::setValue(QString("uilang"), QString::fromUtf8("中文")));
    const QString got = Config::uiLang();
    CHECK(got == QString::fromUtf8("中文"));
    CHECK(got.length() == 2);                 // 2 个汉字，不是 6 个字节
    CHECK((int)got.at(0).unicode() == 0x4E2D); // 中
    CHECK((int)got.at(1).unicode() == 0x6587); // 文
    // 键名也是 UTF-8
    CHECK(Config::setValue(QString::fromUtf8("键名"), QString::fromUtf8("值")));
    CHECK(Config::value(QString::fromUtf8("键名")) == QString::fromUtf8("值"));
}

TEST_CASE("Config: 键名含 JSON 特殊字符仍能正确存取")
{
    FakeHome h;
    const QString key = QString::fromUtf8("a\"b\\c");
    CHECK(Config::setValue(key, QString::fromUtf8("v")));
    CHECK(Config::value(key) == QString::fromUtf8("v"));
    // 原始 JSON 里该键被正确转义
    const std::string raw = h.readRawConfig();
    CHECK(raw.find("\\\"") != std::string::npos);
    CHECK(raw.find("\\\\") != std::string::npos);
}
