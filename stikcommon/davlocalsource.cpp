#include "davlocalsource.h"

DavLocalSource::~DavLocalSource()
{
}

// 进程级注册单例。静态局部 + 首次初始化，C++11 起线程安全初始化；
// 本项目的 Qt3 构建用默认 g++ 标准（gnu++14 及以上），故不额外加锁。
// 需在 -std=c++11 以上编译；qlstik 的 qmake 默认满足。
namespace {

DavLocalSource* g_source = nullptr;
DavLocalNullSource g_nullSource;

} // namespace

void setDavLocalSource(DavLocalSource* source)
{
    g_source = source;
}

DavLocalSource* davLocalSource()
{
    return g_source ? g_source : &g_nullSource;
}

// ── 空对象：批次 5 接入前的缺省行为 ──
// 全部返回「空」而非抛错/崩溃：davbisync 阶段 A 拿到空本地清单后会走
// 「无待传文件」路径；ensurePack 返回空串让调用方判失败（见 davbisync.cpp
// 对 ensurePack 返回值的处理）；importStickerFile 写 errorOut 让上层能
// 报出「导入失败」而不是无声吞掉。
std::vector<DavLocalPack> DavLocalNullSource::installedPacksByTitleAsc()
{
    return std::vector<DavLocalPack>();
}

std::vector<DavLocalSticker> DavLocalNullSource::stickersOfPack(
    const QString& packId)
{
    Q_UNUSED(packId);
    return std::vector<DavLocalSticker>();
}

bool DavLocalNullSource::isBuiltinSourcePack(const QString& packId,
                                             const QString& title)
{
    Q_UNUSED(packId);
    Q_UNUSED(title);
    // 没有库就无从判定「内置源包」；报 false 意味着「不当内置源处理」，
    // 也就是会尝试列出/上传。批次 5 接入后本条不再走。
    return false;
}

QString DavLocalNullSource::builtinSourceDiag(const QString& packId,
                                              const QString& title)
{
    Q_UNUSED(packId);
    Q_UNUSED(title);
    return QString::fromUtf8("local sticker store unavailable (batch 5 not wired)");
}

QStringList DavLocalNullSource::builtinSourceCloudDirs()
{
    return QStringList();
}

QString DavLocalNullSource::relativeToBase(const QString& abs)
{
    // 无 base 可言，按 StickerStore 的约定「无法归一则原样返回」。
    return abs;
}

QString DavLocalNullSource::resolveStickerPath(const QString& stored)
{
    return stored;
}

QString DavLocalNullSource::ensurePack(const QString& title)
{
    Q_UNUSED(title);
    return QString();
}

bool DavLocalNullSource::importStickerFile(const QString& packId,
                                           const QString& srcAbs,
                                           QString* errorOut,
                                           const QString& dstName,
                                           const QString& targetRel)
{
    Q_UNUSED(packId);
    Q_UNUSED(srcAbs);
    Q_UNUSED(dstName);
    Q_UNUSED(targetRel);
    if (errorOut) {
        // 非 ASCII 走 fromUtf8：Qt3 的 QString(const char*) 按 Latin-1 解释，
        // 会把 UTF-8 字节拆成一串 U+00xx 假字符。
        *errorOut = QString::fromUtf8("本地贴纸库不可用（批次 5 未接入）");
    }
    return false;
}
