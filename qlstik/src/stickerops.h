#ifndef QLSTIK_STICKEROPS_H
#define QLSTIK_STICKEROPS_H

// 贴纸项右键菜单的底层操作（anysk StickerStore 的 Qt3 薄封装）。
//
// 只做「读文件 / 解码 / 写 DB」，不碰 UI：一律返回 bool，toast 与弹窗由调用方
// （StickerListPage）发起 —— 静态类拿不到 QWidget* parent。
//
// ⚠ 与 anysk 的三处有意差异（不是遗漏，改动时勿"顺手对齐"）：
//   1. anysk 用 probeImageValidity() 精确嗅探格式，那是 anystik/src 私有函数、
//      不在 stikcommon，故不搬；改用 qmimedatabase_shim + QImageReader 垫片，
//      对本菜单要的三个字段（类型/尺寸/帧数）够用。
//   2. anysk 的 copyStickerScaledToClipboard 走 buildGifBytes/
//      buildApngFromFrames 保留动画；这里只出首帧，与本文件 copyToClipboard
//      及现有单击复制（stickerlistpage.cpp:1232-1234 同样只读首帧）保持一致。
//   3. anysk 的 deleteSticker 也只调 stickerDb().delete_sticker()（软删，
//      不删文件）—— 逐字照搬，不要补文件删除。

#include "compat34.h"

#ifdef QT3_BUILD
#include <qstring.h>
#else
#include <QString>
#endif

// 对齐 anystik/src/stickerstore.h:67 的 StickerMeta，但去掉 QDateTime 成员：
// Qt3/Qt4+ 取格式化结果的写法不同（qdatetime_shim 的 qFormatDateTime vs
// QDateTime::toString），改成在 .cpp 里直接格式化好存 QString，调用侧零分支。
struct StickerMetaLite {
    StickerMetaLite()
        : animated(false), frames(0), width(0), height(0), sizeBytes(0) {}
    QString   typeLabel;      // "GIF image (image/gif)"
    QString   mime;           // "image/gif"
    bool      animated;
    int       frames;         // >1 即动画
    int       width;
    int       height;
    long long sizeBytes;
    QString   modified;       // 已格式化为 "yyyy-MM-dd hh:mm:ss"
};

class StickerOps {
public:
    // 读原图进剪贴板。失败返回 false，调用方负责 toast。
    static bool copyToClipboard(const QString& filePath);

    // 按比例缩放后进剪贴板。只出首帧（见文件头差异 2）。
    static bool copyScaledToClipboard(const QString& filePath, double scale);

    // 采集文件元信息。文件不存在/读不出返回 false，out 保持默认构造。
    static bool collectMeta(const QString& filePath, StickerMetaLite& out);

    // 拼 anysk formatStickerMeta 的同款 5 行文本。
    static QString formatMeta(const StickerMetaLite& m);

    // 三个 DB 写操作。id 即贴纸主键（内容 sha1）。
    static bool setDescription(const QString& id, const QString& desc);
    static bool touch(const QString& id);
    static bool remove(const QString& id);
};

#endif // QLSTIK_STICKEROPS_H