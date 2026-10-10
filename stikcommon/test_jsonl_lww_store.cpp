#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qdatetime.h>
#include <unistd.h>

#include <string>

#include "doctest/doctest.h"

#include "qglobaltype_shim.h"
#include "qstring_shim.h"
#include "qdir_shim.h"
#include "jsonl_lww_store.h"

#include "../qlcomp/compatcore34.h"   // qMkdir(path, recursive)

using namespace jsonl_lww;

namespace {

// Qt3 的 QMap 没有 value(key)：用 find 取（缺失 → 空 QJsonObject）。
QJsonObject getObj(const QMap<QString, QJsonObject>& m, const char* key)
{
    const QMap<QString, QJsonObject>::const_iterator it =
        m.find(QString::fromLatin1(key));
    return it == m.constEnd() ? QJsonObject() : *it;
}

QString descOf(const QMap<QString, QJsonObject>& m, const char* key)
{
    return getObj(m, key).value(QString::fromLatin1("desc")).toString();
}

QString tempDirAbs()
{
    const QString p = QDir(qDirTempPath()).absFilePath(
        QString::fromLatin1("qlstik_jsonl_")
        + QString::number(int(getpid())) + QChar('_')
        + QString::number(int(getpid() ^ 0x5a5a)));
    qMkdir(p, true);
    return p;
}

} // namespace

TEST_CASE("jsonl: 同 key 取 mtime 大者")
{
    const std::string a =
        "{\"rel\":\"a.png\",\"desc\":\"old\",\"mtime\":100}\n"
        "{\"rel\":\"b.png\",\"desc\":\"bb\",\"mtime\":100}\n"
        "{\"rel\":\"a.png\",\"desc\":\"new\",\"mtime\":200}\n";
    const QMap<QString, QJsonObject> m = parse(a);
    CHECK_EQ(int(m.size()), 2);
    CHECK(descOf(m, "a.png") == QString::fromLatin1("new"));
    CHECK(getObj(m, "a.png").value(QString::fromLatin1("mtime")).toDouble() == 200.0);
}

TEST_CASE("jsonl: 合并两段 = 行并集 + 逐记录 LWW")
{
    QMap<QString, QJsonObject> m = parse(
        "{\"rel\":\"a.png\",\"desc\":\"old\",\"mtime\":100}\n"
        "{\"rel\":\"b.png\",\"desc\":\"bb\",\"mtime\":100}\n");
    foldInto(m, std::string(
        "{\"rel\":\"a.png\",\"desc\":\"new\",\"mtime\":200}\n"
        "{\"rel\":\"c.png\",\"desc\":\"cc\",\"mtime\":50}\n"));
    CHECK_EQ(int(m.size()), 3);
    CHECK(descOf(m, "a.png") == QString::fromLatin1("new"));
    CHECK(m.contains(QString::fromLatin1("c.png")));
}

TEST_CASE("jsonl: tie-break = desc 字典序大者（确定性）")
{
    const QMap<QString, QJsonObject> m = parse(
        "{\"rel\":\"x\",\"desc\":\"aa\",\"mtime\":7}\n"
        "{\"rel\":\"x\",\"desc\":\"zz\",\"mtime\":7}\n");
    CHECK(descOf(m, "x") == QString::fromLatin1("zz"));
    const QMap<QString, QJsonObject> n = parse(
        "{\"rel\":\"x\",\"desc\":\"zz\",\"mtime\":7}\n"
        "{\"rel\":\"x\",\"desc\":\"aa\",\"mtime\":7}\n");
    CHECK(descOf(n, "x") == QString::fromLatin1("zz"));
}

TEST_CASE("jsonl: 坏行 / 非对象 / 缺 rel 跳过")
{
    const QMap<QString, QJsonObject> m = parse(
        "not json\n"
        "[1,2,3]\n"
        "{\"desc\":\"no rel\"}\n"
        "{\"rel\":\"ok\",\"desc\":\"d\",\"mtime\":1}\n");
    CHECK_EQ(int(m.size()), 1);
    CHECK(m.contains(QString::fromLatin1("ok")));
}

TEST_CASE("jsonl: 末行无换行符也算一条")
{
    const QMap<QString, QJsonObject> m =
        parse("{\"rel\":\"z\",\"desc\":\"d\",\"mtime\":9}");
    CHECK_EQ(int(m.size()), 1);
    CHECK(m.contains(QString::fromLatin1("z")));
}

TEST_CASE("jsonl: 序列化确定性且可回环")
{
    const std::string a =
        "{\"rel\":\"a.png\",\"desc\":\"x\",\"mtime\":2}\n"
        "{\"rel\":\"b.png\",\"desc\":\"y\",\"mtime\":1}\n";
    const QMap<QString, QJsonObject> m = parse(a);
    const std::string s = serialize(m);
    CHECK(s == serialize(m));
    const QMap<QString, QJsonObject> round = parse(s);
    CHECK_EQ(int(round.size()), 2);
    CHECK(descOf(round, "a.png") == QString::fromLatin1("x"));
}

// ⚠ 非 ASCII 必须走 UTF-8 并能无损回环（AGENTS.md：不用 latin1，按码点验证）。
TEST_CASE("jsonl: 非 ASCII UTF-8 回环（按码点）")
{
    // 用 setDesc 风格手工造一条中文描述：你好 = U+4F60 U+597D
    const char kHello[] = "\xe4\xbd\xa0\xe5\xa5\xbd";
    QJsonObject o;
    o.insert(QString::fromLatin1("rel"), QJsonValue(QString::fromLatin1("a.png")));
    o.insert(QString::fromLatin1("desc"), QJsonValue(QString::fromUtf8(kHello, 6)));
    o.insert(QString::fromLatin1("mtime"), QJsonValue(double(5)));
    QMap<QString, QJsonObject> m;
    m.insert(QString::fromLatin1("a.png"), o);

    const QMap<QString, QJsonObject> back = parse(serialize(m));
    const QString d = descOf(back, "a.png");
    CHECK_EQ(d.length(), 2);
    CHECK(d.unicode()[0] == QChar(0x4F60));   // 你
    CHECK(d.unicode()[1] == QChar(0x597D));   // 好
}

TEST_CASE("jsonl: writeFile / readFileRaw 磁盘回环")
{
    const QString dir = tempDirAbs();
    const QString path = dir + QString::fromLatin1("/_stikmeta.svg");
    QMap<QString, QJsonObject> m = parse(
        "{\"rel\":\"a.png\",\"desc\":\"hello\",\"mtime\":42}\n");
    CHECK(writeFile(path, m));
    const std::string raw = readFileRaw(path);
    CHECK(!raw.empty());
    const QMap<QString, QJsonObject> back = parse(raw);
    CHECK(descOf(back, "a.png") == QString::fromLatin1("hello"));
    qDirRemoveRecursively(QDir(dir));
}
