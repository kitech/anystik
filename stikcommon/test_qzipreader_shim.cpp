// stikcommon/test_qzipreader_shim.cpp —— QZipReader 垫片契约（Qt3 单端）
//
// 被测对象：qzipreader_shim.h/.cpp —— Qt3 无 QZipReader（Qt 的私有 zip 读取器）。
//   产品用它读取 .eif 贴纸包（本质是 zip）。
//
// 为什么必须用**真实 zip 字节**测：本垫片要自己解析 EOCD / 中央目录 / 本地头
//   三层结构。若只测「不存在的文件返回 false」这类错误路径，等于没测解析器；
//   而解析器一旦算错偏移，症状是**读出的文件内容错位或为空**，不会有异常。
//
// 本文件内嵌的 322 字节是一个用 python3 zipfile(ZIP_STORED) 生成的真实、
//   合规 zip，含三个条目（按中央目录顺序）：
//       hello.txt       内容 "hello zip world"（15 字节）
//       sub/            目录项
//       sub/inner.bin   内容 00 01 02 fe ff（5 字节，含二进制）
//   ZIP_STORED（method=0，未压缩）保证不依赖 zlib 的解压分支，专测结构解析。
//
// ⚠ 已核实：本垫片解压**不校验 CRC**（qzipreader_shim.cpp 的 fileDataByIndex
//   无 crc 比对），但内嵌 zip 仍是带真实 CRC 的合规文件，避免制造「靠不校验
//   才过」的假象。

#include <qstring.h>
#include <qcstring.h>
#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qstringlist.h>
#include <unistd.h>

#include "doctest/doctest.h"

#include "qglobaltype_shim.h"    // Qt3 的 qint64（FileInfo::size 用）
#include "qlist_shim.h"          // QZipReader::fileInfoList 返回 QList
#include "qdir_shim.h"
#include "qzipreader_shim.h"
#include "qba_shim.h"
#include "../qlcomp/compatcore34.h"   // qMkdir

// 真实合规 zip（python3 zipfile / ZIP_STORED 生成，322 字节）
static const unsigned char kZipBytes[] = {
    0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8c, 0x95,
    0x42, 0x5d, 0x5d, 0x89, 0xc2, 0xda, 0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00,
    0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x2e,
    0x74, 0x78, 0x74, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x7a, 0x69, 0x70,
    0x20, 0x77, 0x6f, 0x72, 0x6c, 0x64, 0x50, 0x4b, 0x03, 0x04, 0x14, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x8c, 0x95, 0x42, 0x5d, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x73, 0x75, 0x62, 0x2f, 0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x8c, 0x95, 0x42, 0x5d, 0xa8, 0x67, 0x27, 0xda, 0x05, 0x00,
    0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x73, 0x75,
    0x62, 0x2f, 0x69, 0x6e, 0x6e, 0x65, 0x72, 0x2e, 0x62, 0x69, 0x6e, 0x00,
    0x01, 0x02, 0xfe, 0xff, 0x50, 0x4b, 0x01, 0x02, 0x14, 0x03, 0x14, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x8c, 0x95, 0x42, 0x5d, 0x5d, 0x89, 0xc2, 0xda,
    0x0f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x2e, 0x74, 0x78, 0x74, 0x50,
    0x4b, 0x01, 0x02, 0x14, 0x03, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8c,
    0x95, 0x42, 0x5d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x00, 0xfd, 0x41, 0x36, 0x00, 0x00, 0x00, 0x73, 0x75, 0x62,
    0x2f, 0x50, 0x4b, 0x01, 0x02, 0x14, 0x03, 0x14, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x8c, 0x95, 0x42, 0x5d, 0xa8, 0x67, 0x27, 0xda, 0x05, 0x00, 0x00,
    0x00, 0x05, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x58, 0x00, 0x00, 0x00, 0x73,
    0x75, 0x62, 0x2f, 0x69, 0x6e, 0x6e, 0x65, 0x72, 0x2e, 0x62, 0x69, 0x6e,
    0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00,
    0xa4, 0x00, 0x00, 0x00, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static const int kZipLen = (int)sizeof(kZipBytes);

namespace {

class TempDir {
public:
    TempDir()
    {
        m_path = QDir(qDirTempPath()).absFilePath(
            QString::fromLatin1("qlstik_qzip_")
            + QString::number(int(getpid())) + QChar('_')
            + QString::number(++s_counter()));
        qMkdir(m_path, true);
    }
    ~TempDir() { qDirRemoveRecursively(QDir(m_path)); }
    QString path() const { return m_path; }
    QString file(const char* name) const
    { return QDir(m_path).absFilePath(QString::fromLatin1(name)); }

    void writeZip(const char* name) const
    {
        QFile f(file(name));
        f.open(IO_WriteOnly | IO_Truncate);
        f.writeBlock((const char*)kZipBytes, (Q_ULONG)kZipLen);
        f.close();
    }
    void writeGarbage(const char* name) const
    {
        QFile f(file(name));
        f.open(IO_WriteOnly | IO_Truncate);
        f.writeBlock("not a zip at all", 16);
        f.close();
    }
    QByteArray read(const QString& absPath) const
    {
        QFile f(absPath);
        if (!f.open(IO_ReadOnly)) return QByteArray();
        const QByteArray b = f.readAll();
        f.close();
        return b;
    }
private:
    static int& s_counter() { static int c = 0; return c; }
    QString m_path;
};

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 解析中央目录
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("QZipReader: 真实 zip 可读，条目按中央目录顺序列出（含目录项）")
{
    TempDir td;
    td.writeZip("pack.eif");

    QZipReader zr(td.file("pack.eif"));
    CHECK(zr.isReadable());
    CHECK(zr.exists());
    CHECK_EQ(zr.status(), QZipReader::NoError);

    const QList<QZipReader::FileInfo> files = zr.fileInfoList();
    REQUIRE_EQ(files.count(), 3);                 // 文件+目录+文件，一个都不能丢

    CHECK_EQ(files.at(0).filePath, QString("hello.txt"));
    CHECK(files.at(0).isFile);
    CHECK(!files.at(0).isDir);
    CHECK_EQ(files.at(0).size, (qint64)15);

    CHECK_EQ(files.at(1).filePath, QString("sub"));   // 目录项的尾 '/' 被剥掉
    CHECK(files.at(1).isDir);
    CHECK(!files.at(1).isFile);

    CHECK_EQ(files.at(2).filePath, QString("sub/inner.bin"));
    CHECK(files.at(2).isFile);
    CHECK_EQ(files.at(2).size, (qint64)5);
}

// ⚠ FileInfo::isValid 只在三类之一为真时为真；防止结构解析出「既非文件也非
//   目录」的幽灵条目。
TEST_CASE("QZipReader: 每个条目都是文件或目录（isValid）")
{
    TempDir td;
    td.writeZip("pack.eif");
    QZipReader zr(td.file("pack.eif"));
    const QList<QZipReader::FileInfo> files = zr.fileInfoList();
    for (int i = 0; i < files.count(); ++i) {
        CHECK(files.at(i).isValid());
        // 文件与目录互斥（同一条目不能两者都是）
        CHECK(!(files.at(i).isFile && files.at(i).isDir));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// extractAll —— 真正解出内容（结构偏移错了这里必现）
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("QZipReader: extractAll 解出文件内容与目录结构")
{
    TempDir td;
    td.writeZip("pack.eif");

    QZipReader zr(td.file("pack.eif"));
    const QString dest = td.file("out");
    REQUIRE(qMkdir(dest, true));
    CHECK(zr.extractAll(dest));

    // 普通文件内容必须逐字一致（偏移算错会得到空或错位内容）
    CHECK_EQ(td.read(dest + QString::fromLatin1("/hello.txt")),
             qbaLit("hello zip world"));

    // 目录项应被建出
    CHECK(QFileInfo(dest + QString::fromLatin1("/sub")).isDir());

    // 二进制内容（含 0xff / 0x00）必须原样，不得被当字符串截断
    const QByteArray inner = td.read(dest + QString::fromLatin1("/sub/inner.bin"));
    REQUIRE_EQ(inner.size(), 5);
    CHECK_EQ((unsigned char)inner[0], 0x00);
    CHECK_EQ((unsigned char)inner[1], 0x01);
    CHECK_EQ((unsigned char)inner[2], 0x02);
    CHECK_EQ((unsigned char)inner[3], 0xfe);
    CHECK_EQ((unsigned char)inner[4], 0xff);
}

// ⚠ 未压缩（store）条目必须原样拷贝：内容是 "hello zip world"，若误走解压
//   分支会失败或得到空。上面已覆盖；这里再确认长度（防截断）。
TEST_CASE("QZipReader: store 条目长度正确（不截断、不补零）")
{
    TempDir td;
    td.writeZip("pack.eif");
    QZipReader zr(td.file("pack.eif"));
    const QString dest = td.file("out");
    REQUIRE(qMkdir(dest, true));
    REQUIRE(zr.extractAll(dest));
    CHECK_EQ(td.read(dest + QString::fromLatin1("/hello.txt")).size(), 15);
}

// ═══════════════════════════════════════════════════════════════════════════
// 错误路径
// ═══════════════════════════════════════════════════════════════════════════
// ⚠ 头注释（qzipreader_shim.h:89-90）明确：isReadable() 只看设备能否读，
//   **不看 status**，故「存在但不是 zip」也是 true。status 保持 NoError
//   （scanFiles 遇签名不符直接 return，不改 status）。这两条与 Qt6 一致，
//   是容易「顺手改成 false」的地方。

TEST_CASE("QZipReader: 不存在的文件 → isReadable/exists 均 false，条目为空")
{
    TempDir td;
    QZipReader zr(td.file("absent.eif"));
    CHECK(!zr.isReadable());
    CHECK(!zr.exists());
    CHECK_EQ(zr.fileInfoList().count(), 0);
}

TEST_CASE("QZipReader: 存在但非 zip → isReadable 仍 true，但无条目")
{
    TempDir td;
    td.writeGarbage("garbage.bin");

    QZipReader zr(td.file("garbage.bin"));
    CHECK(zr.isReadable());                        // 关键：设备可读即 true
    CHECK(zr.exists());
    CHECK_EQ(zr.fileInfoList().count(), 0);        // 签名不符，解析不出条目
    CHECK_EQ(zr.status(), QZipReader::NoError);    // 解析失败不改 status（Qt 行为）
}

// ⚠ close() 与 Qt 一致：只关设备，**不清索引**，故 close 后 fileInfoList 仍
//   返回已扫描的条目。这条防「close 顺手 clear」的改动破坏调用点。
TEST_CASE("QZipReader: close 后索引仍在（fileInfoList 不退化为空）")
{
    TempDir td;
    td.writeZip("pack.eif");
    QZipReader zr(td.file("pack.eif"));
    REQUIRE_EQ(zr.fileInfoList().count(), 3);
    zr.close();
    CHECK_EQ(zr.fileInfoList().count(), 3);        // 仍能列条目
}
