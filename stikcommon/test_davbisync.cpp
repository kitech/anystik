// stikcommon/test_davbisync.cpp —— SyncEngine 链接/生命周期冒烟（Qt3 单端）

#include <qstring.h>
#include <qstringlist.h>

#include "doctest/doctest.h"

#include "davlocalsource.h"
#include "davbisync.h"

namespace {

class FakeLocalSource : public DavLocalSource {
public:
    QString baseDir() const { return QStringLiteral("/tmp/fake-base"); }
    std::vector<DavLocalPack> installedPacksByTitleAsc() { return {}; }
    std::vector<DavLocalSticker> stickersOfPack(const QString&) { return {}; }
    bool isBuiltinSourcePack(const QString&, const QString&) { return false; }
    QString builtinSourceDiag(const QString&, const QString&) { return QString(); }
    QStringList builtinSourceCloudDirs() { return QStringList(); }
    QString relativeToBase(const QString& abs) { return abs; }
    QString resolveStickerPath(const QString& stored) { return stored; }
    QString ensurePack(const QString&) { return QString(); }
    bool importStickerFile(const QString&, const QString&, QString*,
                           const QString&, const QString&) { return true; }
};

} // namespace

TEST_CASE("davbisync: 默认状态")
{
    SyncEngine e;
    CHECK(e.isRunning() == false);
    CHECK(e.progressPercent() == 0);
}

TEST_CASE("davbisync: setConnectionSettings 回读")
{
    SyncEngine e;
    const bool ok = e.setConnectionSettings(
        /*connectionType*/ 2, QStringLiteral("dav.example.com"),
        QStringLiteral("/dav"), QStringLiteral("alice"),
        QStringLiteral("s3cr3t"), 8443, /*ssl*/ true);
    CHECK(ok);
    CHECK(e.isCloudAvailable() == false);
}

TEST_CASE("davbisync: setDavLocalSource 注册真实现账号")
{
    FakeLocalSource src;
    setDavLocalSource(&src);
    CHECK(davLocalSource() == &src);
    setDavLocalSource(nullptr);
    CHECK(davLocalSource() != nullptr);
}

TEST_CASE("davbisync: abort 幂等")
{
    SyncEngine e;
    e.abort();
    e.abort();
    CHECK(e.isRunning() == false);
}
