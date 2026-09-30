#ifndef DAVLOCALSOURCE_H
#define DAVLOCALSOURCE_H

// davlocalsource.h —— davbisync 面向「本地贴纸库」的窄切面接口。
//
// 为什么要这层（背景是实测的依赖面，不是设计洁癖）：
//   davbisync.cpp 原本 `#include "stickerstore.h"` 并在约 20 处调
//   StickerStore::instance()（建本地清单、云目录→包映射、下载落盘、冲突改名…）。
//   而 stickerstore.cpp 是 4196 行，自身还拖着 eifreader / storage /
//   sticker_db / androidutils(JNI) / macpasteboard / macgifconverter /
//   qnaturalsort / QtConcurrent / QClipboard / QMimeDatabase /
//   QtCore-private qzipreader / tangora_gif / zlib —— 它自己都还没适配 Qt3，
//   是批次 5 的地盘。若 3c-2 直接依赖它，本批就得把批次 5 一起拖进来。
//
//   故这里抽出 davbisync 实际用到的 9 个方法（已逐一核实调用点，见下方
//   「切面依据」），davbisync.cpp 改依赖本接口；批次 5 再把真 StickerStore
//   接进来注册即可，davbisync 一行不用再改。
//
// 切面依据（`grep -o "StickerStore::instance()->[a-zA-Z]*"` 的去重全量，共 9 个；
//  数据字段也只用到 2 个，已逐个核实）：
//   packs(id,title) → 只需 pack.id / pack.title
//   stickers(packId) → 只需 s.filePath（davbisync.cpp:333 用 s.filePath 当绝对路径）
//   其余 7 个是标量/字符串/列表，直接照搬。
//   → 所以本接口**不复刻** StickerBrief / StickerPackBrief 全字段，只给这两个
//     结构体各留所需字段。批次 5 接真实现时由适配层负责裁剪。
//
// 为什么用 std::vector 而不是 QList：本项目的 Qt3 垫片面已有一条明确原则
// （见 dav207iface.h）——**接口契约不绑 Qt 容器**。实测原因是 Qt 3.5 的
// `QList` 只是 `#define QList QPtrList` 的指针宏（qptrlist.h:189），存不了值
// 类型；dav207iface 与 QWebdavDirParserLite::getList() 都已改用 std::vector
// 出参绕开。本接口沿用，避免把 qlist_shim 的指针宏雷区引进新代码。
// 代价：davbisync.cpp 侧消费点要写 range-for（QList 在 Qt3 下
// `for (auto& x : list)` 也不安全，因为 Qt3 的 begin()/end() 语义不同）。
//
// 生命周期：davLocalSource() 返回的是**进程级注册单例**，非空
// （未注册真实现时返回空对象 DavLocalNullSource）。故 davbisync 的每个调用点
// 都可以直接 `davLocalSource()->xxx()`，不必判空——这与原先
// `StickerStore::instance()->xxx()` 的写法同形，移植 diff 因此是机械替换。
// 空对象在 3c-2（真实现属批次 5，尚未接入）下会让阶段 A 拿到空本地清单，
// SyncEngine 走「无待传文件」路径而不是崩溃或静默写坏基线。

// 跨版本头：davbisync.cpp 要在 Qt3 与 Qt6 下都编，故 QString/QStringList
// 必须条件包含。样板照 stikcommon/davobfus.h:4-8（已双端在编的既有头），
// 不自创写法。Qt3 的小写头 <qstring.h> 在 Qt6 下不存在，反之亦然。
#ifdef QT3_BUILD
#include <qstring.h>
#include <qstringlist.h>
#else
#include <QString>
#include <QStringList>
#endif

#include <vector>

// 贴纸包窄视图：只需 id / title。
// 不复刻 StickerPackBrief（那还有 installed/created_at/… 全字段，davbisync
// 一处不读）。批次 5 的适配层负责从真结构体里挑字段填进来。
struct DavLocalPack
{
    QString id;
    QString title;
};

// 贴纸窄视图：只需 filePath（resolve 后的**绝对**路径）。
// 不复刻 StickerBrief（那还有 id/emoji/width/height/description/isPublic…）。
// 注意语义：davbisync.cpp:333 明确要求此处已是绝对路径，**不要再 resolve**。
struct DavLocalSticker
{
    QString filePath;
};

// davbisync 需要的本地贴纸库能力。全部按值返回，不暴露真实现的类型，
// 因此批次 5 可以换存储后端（sqlite → 别的东西）而不动 davbisync。
//
// 非 const 命名的方法在真 StickerStore 里同样是 non-const，这里保持一致，
// 免得适配层要额外加 const 重载转发。
class DavLocalSource
{
public:
    virtual ~DavLocalSource();

    // ── 查询 ──
    // 「已安装的包，按 title 升序」。对应 StickerStore::packs(1, "title ASC")：
    // installed=1 过滤与 title 排序是 davbisync 清单顺序的一部分，烤进方法名，
    // 免得调用点漏传又变成「全包 + 默认排序」这种静默行为变化。
    virtual std::vector<DavLocalPack> installedPacksByTitleAsc() = 0;

    // 某包下的贴纸。对应 StickerStore::stickers(packId)（用其默认 rowid DESC）。
    virtual std::vector<DavLocalSticker> stickersOfPack(const QString& packId) = 0;

    // 是否为「内置下载源」的包（上传侧整包不列出、下载侧跳过）。
    virtual bool isBuiltinSourcePack(const QString& packId,
                                     const QString& title) = 0;

    // 诊断辅助（只读）：格式为
    //   id=.. title=.. meta.url=.. urlNorm=.. urlHit=YES/NO builtinTitleHit=YES/NO
    // 供定位「内置源包为何未被排除上传」。
    virtual QString builtinSourceDiag(const QString& packId,
                                      const QString& title) = 0;

    // 全部内置源包对应的云端目录名集合（sanitizeDirName(title)）。
    // 同步两侧共用：上传侧整包不列出、下载侧对命中的云目录跳过拉取。
    virtual QStringList builtinSourceCloudDirs() = 0;

    // resolveStickerPath 的反向：绝对路径 → 相对 base 的相对路径
    // （base 外 / 绝对路径原样返回）。
    virtual QString relativeToBase(const QString& abs) = 0;

    // 由 DB 存储的相对路径还原为绝对路径（相对 base，绝对路径透传）。
    virtual QString resolveStickerPath(const QString& stored) = 0;

    // ── 写（下行落地）──
    // 按标题复用或新建贴纸包，返回 packId；失败返回空串。
    virtual QString ensurePack(const QString& title) = 0;

    // 把云端 get 落地的临时文件移入目标目录并入库。
    //   targetRel 非空 → 落盘到该相对路径（业务 dbRel，与下载侧键一致）：
    //     pastes/<f>     → base/pastes/<f>
    //     packs/<T>/<f>  → base/packs/<T>/<f>
    //   为空 → 沿用 base/packs/<title>/<fileName>
    //   dstName 非空 → 用其作目标文件名（下载侧传云端原始 basename，
    //                  避免以临时文件名落盘）；为空则沿用 srcAbs 文件名。
    // 幂等：目标相对路径已有 sticker 行则直接返回 true（不动字节）。
    virtual bool importStickerFile(const QString& packId,
                                   const QString& srcAbs,
                                   QString* errorOut,
                                   const QString& dstName,
                                   const QString& targetRel) = 0;
};

// 注册真实现（批次 5 由 StickerStore 适配层调用）。
// 传 nullptr 退回空对象；重复注册以最后一次为准。
void setDavLocalSource(DavLocalSource* source);

// 取当前实现。**恒非空**：未注册时返回 DavLocalNullSource。
// 故调用点不必判空。
DavLocalSource* davLocalSource();

// 空对象：批次 5 接入前的缺省值。
// 语义是「本地贴纸库不可用」而不是「库是空的」——但对 davbisync 而言两者
// 效果相同（本地清单为空 → 无待传文件），故不额外区分，避免在 3c-2 阶段
// 就为将来才存在的状态设计分支。
class DavLocalNullSource : public DavLocalSource
{
public:
    std::vector<DavLocalPack> installedPacksByTitleAsc();
    std::vector<DavLocalSticker> stickersOfPack(const QString& packId);
    bool isBuiltinSourcePack(const QString& packId, const QString& title);
    QString builtinSourceDiag(const QString& packId, const QString& title);
    QStringList builtinSourceCloudDirs();
    QString relativeToBase(const QString& abs);
    QString resolveStickerPath(const QString& stored);
    QString ensurePack(const QString& title);
    bool importStickerFile(const QString& packId, const QString& srcAbs,
                           QString* errorOut, const QString& dstName,
                           const QString& targetRel);
};

#endif // DAVLOCALSOURCE_H
