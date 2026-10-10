// stikcommon/test_pack_meta_store.cpp —— 包级描述载体（_stikmeta.svg）单元测试
//
// 用一个纯内存 FakeDb 实现 StickerDbSyncInterface，覆盖：
//   * carrierDir / carrierRel 推导（普通包 "packs/<T>" 与粘贴板包 "pastes"）
//   * basenameOf / isCarrierBase / isInternalRel（UI 过滤判定）
//   * setDescQ → 建载体行 + 落盘；applyPack 回写 DB 描述
//   * removeDesc 移行
//   * resolveConflicts：把 *.conflict<digits> 内容并入主载体，且
//     **保留冲突文件本身与冲突行**（引擎不删远端，删本地会触发重复下载）
#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

#include "doctest/doctest.h"

#include "qglobaltype_shim.h"
#include "qstring_shim.h"
#include "qdir_shim.h"
#include "sticker_db.h"
#include "pack_meta_store.h"
#include "jsonl_lww_store.h"

#include "../qlcomp/compatcore34.h"   // qMkdir

namespace {

class FakeDb : public StickerDbSyncInterface {
public:
    std::vector<StickerPackRow> packs;
    std::vector<StickerRow> rows;

    bool add_pack(const StickerPackRow& p) override { packs.push_back(p); return true; }
    bool delete_pack(const char*) override { return true; }
    bool update_pack_position(const char*, int) override { return true; }
    bool update_pack_installed(const char*, int) override { return true; }
    std::unique_ptr<StickerPackRow> get_pack(const char* id) override {
        for (size_t i = 0; i < packs.size(); ++i)
            if (packs[i].id == id) return std::unique_ptr<StickerPackRow>(new StickerPackRow(packs[i]));
        return std::unique_ptr<StickerPackRow>();
    }
    std::vector<StickerPackRow> list_packs(int, const char*, int, int) override { return packs; }

    bool add_sticker(const StickerRow& s) override { rows.push_back(s); return true; }
    bool delete_sticker(const char* id) override {
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].id == id) rows[i].deleted = 1;
        return true;
    }
    bool delete_stickers_by_pack(const char*) override { return true; }
    bool touch_sticker(const char*, int64_t) override { return true; }
    bool update_sticker_description(const char* id, const char* desc) override {
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].id == id) { rows[i].description = desc; return true; }
        return false;
    }
    std::unique_ptr<StickerRow> get_sticker(const char* id) override {
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].id == id) return std::unique_ptr<StickerRow>(new StickerRow(rows[i]));
        return std::unique_ptr<StickerRow>();
    }
    int sticker_deleted_state(const char* id) override {
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].id == id) return rows[i].deleted;
        return -1;
    }
    bool restore_sticker(const char* id) override {
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].id == id) rows[i].deleted = 0;
        return true;
    }
    std::vector<StickerRow> list_stickers(const char* pack_id, const char*, int, int, int deleted, const char*) override {
        std::vector<StickerRow> out;
        for (size_t i = 0; i < rows.size(); ++i) {
            if (pack_id && rows[i].pack_id != pack_id) continue;
            if (deleted >= 0 && rows[i].deleted != deleted) continue;
            out.push_back(rows[i]);
        }
        return out;
    }
    std::vector<StickerRow> list_recent_stickers(int) override { return rows; }
    std::vector<StickerRow> search_stickers(const char*) override { return std::vector<StickerRow>(); }
    int count_stickers(const char* pack_id) override {
        int n = 0;
        for (size_t i = 0; i < rows.size(); ++i)
            if (!pack_id || rows[i].pack_id == pack_id) ++n;
        return n;
    }
    int countPacks() override { return int(packs.size()); }
    bool begin_write_transaction() override { return true; }
    bool commit_transaction() override { return true; }

    void addRow(const char* id, const char* pack, const char* path, const char* desc) {
        StickerRow r;
        r.id = id; r.pack_id = pack; r.file_path = path; r.description = desc; r.deleted = 0;
        rows.push_back(r);
    }
    StickerRow* findRow(const char* id) {
        for (size_t i = 0; i < rows.size(); ++i) if (rows[i].id == id) return &rows[i];
        return 0;
    }
};

QString tempDirAbs()
{
    const QString p = QDir(qDirTempPath()).absFilePath(
        QString::fromLatin1("qlstik_pm_")
        + QString::number(int(getpid())) + QChar('_'));
    qMkdir(p, true);
    return p;
}

std::string carrierRaw(const std::string& baseDir, const std::string& rel)
{
    return jsonl_lww::readFileRaw(
        qFromStdString(baseDir + "/" + rel));
}

QString descIn(const std::string& raw, const char* key)
{
    const QMap<QString, QJsonObject> m = jsonl_lww::parse(raw);
    const QMap<QString, QJsonObject>::const_iterator it =
        m.find(QString::fromLatin1(key));
    return it == m.constEnd() ? QString()
         : (*it).value(QString::fromLatin1("desc")).toString();
}

} // namespace

TEST_CASE("packmeta: basename / isCarrierBase / isInternalRel")
{
    CHECK(packmeta::basenameOf("packs/Mypack/a.png") == "a.png");
    CHECK(packmeta::basenameOf("a.png") == "a.png");
    CHECK(packmeta::basenameOf("pastes/x.gif") == "x.gif");

    CHECK(packmeta::isCarrierBase("_stikmeta.svg"));
    CHECK(packmeta::isCarrierBase("_stikmeta.svg.conflict1"));
    CHECK(packmeta::isCarrierBase("_stikmeta.svg.conflict123"));
    CHECK(!packmeta::isCarrierBase("_stikmeta.svg.conflictX"));
    CHECK(!packmeta::isCarrierBase("a.png"));

    CHECK(packmeta::isInternalRel("packs/Mypack/_stikmeta.svg"));
    CHECK(packmeta::isInternalRel("packs/Mypack/_stikmeta.svg.conflict2"));
    CHECK(!packmeta::isInternalRel("packs/Mypack/a.png"));
}

TEST_CASE("packmeta: carrierDir 普通包 / 粘贴板包")
{
    FakeDb db;
    db.addRow("1", "p1", "packs/Mypack/a.png", "");
    db.addRow("2", "p1", "packs/Mypack/b.png", "");
    CHECK(packmeta::carrierDir(db, "p1") == "packs/Mypack");
    CHECK(packmeta::carrierRel(db, "p1") == "packs/Mypack/_stikmeta.svg");

    FakeDb pdb;
    pdb.addRow("3", "paste", "pastes/c.png", "");
    CHECK(packmeta::carrierDir(pdb, "paste") == "pastes");
    CHECK(packmeta::carrierRel(pdb, "paste") == "pastes/_stikmeta.svg");
}

TEST_CASE("packmeta: setDescQ 建载体行 + 落盘；applyPack 回写描述")
{
    const QString dir = tempDirAbs();
    const std::string base = qToStdString(dir);
    const std::string helloUtf8 = "\xe4\xbd\xa0\xe5\xa5\xbd";   // 你好
    const QString helloQ = QString::fromUtf8(helloUtf8.c_str(), 6);

    FakeDb db;
    db.addRow("1", "p1", "packs/Mypack/a.png", "");
    db.addRow("2", "p1", "packs/Mypack/b.png", "keep");

    CHECK(packmeta::setDescQ(db, base, "p1", "a.png", helloQ, 1234));
    // 载体行已建立
    bool hasCarrier = false;
    for (size_t i = 0; i < db.rows.size(); ++i)
        if (db.rows[i].file_path == "packs/Mypack/_stikmeta.svg") hasCarrier = true;
    CHECK(hasCarrier);

    // 落盘内容（按码点校验非 ASCII）
    const QString desc = descIn(carrierRaw(base, "packs/Mypack/_stikmeta.svg"), "a.png");
    CHECK_EQ(desc.length(), 2);
    CHECK(desc.unicode()[0] == QChar(0x4F60));
    CHECK(desc.unicode()[1] == QChar(0x597D));

    // applyPack 把载体内容回写 DB 描述（模拟下载落地后）
    FakeDb db2;
    db2.addRow("1", "p1", "packs/Mypack/a.png", "");
    db2.addRow("2", "p1", "packs/Mypack/b.png", "keep");
    CHECK(packmeta::applyPack(db2, base, "p1") == 1);
    CHECK(db2.findRow("1")->description == helloUtf8);   // 已回写
    CHECK(db2.findRow("2")->description == "keep");      // 载体里没有 b.png，不动

    qDirRemoveRecursively(QDir(dir));
}

TEST_CASE("packmeta: removeDesc 移行（文件随之重写）")
{
    const QString dir = tempDirAbs();
    const std::string base = qToStdString(dir);

    FakeDb db;
    db.addRow("1", "p1", "packs/Mypack/a.png", "");
    REQUIRE(packmeta::setDesc(db, base, "p1", "a.png", "hello", 10));
    CHECK(descIn(carrierRaw(base, "packs/Mypack/_stikmeta.svg"), "a.png")
          == QString::fromLatin1("hello"));
    REQUIRE(packmeta::removeDesc(db, base, "p1", "a.png"));
    CHECK(descIn(carrierRaw(base, "packs/Mypack/_stikmeta.svg"), "a.png").isEmpty());

    qDirRemoveRecursively(QDir(dir));
}

TEST_CASE("packmeta: resolveConflicts 合并内容但保留冲突文件")
{
    const QString dir = tempDirAbs();
    const std::string base = qToStdString(dir);

    FakeDb db;
    db.addRow("1", "p1", "packs/Mypack/a.png", "");
    db.addRow("2", "p1", "packs/Mypack/b.png", "");

    // 主载体：a.png 旧值 (mtime 100)
    QMap<QString, QJsonObject> main = jsonl_lww::parse(
        "{\"rel\":\"a.png\",\"desc\":\"old\",\"mtime\":100}\n");
    REQUIRE(jsonl_lww::writeFile(
        qFromStdString(base + "/packs/Mypack/_stikmeta.svg"), main));

    // 冲突文件：a.png 新值 (mtime 200) + c.png
    QMap<QString, QJsonObject> conf = jsonl_lww::parse(
        "{\"rel\":\"a.png\",\"desc\":\"new\",\"mtime\":200}\n"
        "{\"rel\":\"c.png\",\"desc\":\"cc\",\"mtime\":50}\n");
    const std::string confRel = "packs/Mypack/_stikmeta.svg.conflict1";
    REQUIRE(jsonl_lww::writeFile(
        qFromStdString(base + "/" + confRel), conf));
    db.addRow("cf", "p1", confRel.c_str(), "");

    CHECK(packmeta::resolveConflicts(db, base, "p1") == 1);

    const std::string merged = carrierRaw(base, "packs/Mypack/_stikmeta.svg");
    CHECK(descIn(merged, "a.png") == QString::fromLatin1("new"));
    CHECK(descIn(merged, "c.png") == QString::fromLatin1("cc"));

    // 冲突文件本身与其 DB 行都必须保留（引擎不删远端）
    CHECK(!carrierRaw(base, confRel).empty());
    CHECK(db.findRow("cf") != 0);

    qDirRemoveRecursively(QDir(dir));
}
