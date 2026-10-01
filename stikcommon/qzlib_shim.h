#ifndef QLSTIK_QZLIB_SHIM_H
#define QLSTIK_QZLIB_SHIM_H

// zlib 压缩垫片 —— **只补 Qt3 一侧**。
//
// 背景：stickerstore.cpp 的 buildApngFromFrames 要把「逐行 filter=none 的原始
// RGBA 扫描线」压成 PNG 里的 zlib 流，调用点写的是 Qt 原生的
// `qCompress(raw, 6)`。
//
// Qt6 侧不需要任何东西：QtCore 自带全局 qCompress(const QByteArray&, int)
// （qbytearray.h:701，另有 (const uchar*, qsizetype, int) 主入口 :699），
// 由 <QtCore/QByteArray> 间接带入，故调用点无分支即可编过 —— 这也是此前
// Qt6 构建通过、唯独 Qt3 报 "'qCompress' was not declared" 的原因。
// 该声明在 `#ifndef QT_NO_COMPRESS` 内；本仓未定义 QT_NO_COMPRESS（已核）。
//
// Qt3 侧 QByteArray 就是 QMemArray<char>，Qt 完全不提供压缩 API，故自己用
// zlib 的 compress2() 实现。zlib 头由调用方（stickerstore.cpp:157）已包含。
//
// 语义对齐 Qt 原生 qCompress：
//   * level 直接透传 zlib（-1 = Z_DEFAULT_COMPRESSION，0 = 不压缩，1..9 越大越压）；
//   * 失败返回**空** QByteArray，不抛异常也不 abort；
//   * 正常返回恰好是压缩后长度的字节串（Qt 原生会额外留一位 '\0'，
//     本实现不留 —— PNG 的 IDAT/zlib 流长度由本函数返回值决定，
//     多留一个 '\0' 反而会让 zlib 流末尾多出无效字节）。

#ifdef QT3_BUILD

#include <qglobal.h>
#include <qcstring.h>
#include <zlib.h>

inline QByteArray qCompress(const QByteArray& raw, int level = -1)
{
    const int n = raw.size();
    if (n <= 0)
        return QByteArray();

    // 一次性按 compressBound() 上界分配，避免反复 realloc + memcpy。
    // ⚠ Qt3 的 QMemArray 无隐式共享（见 qba_shim.h 注记），反复 resize
    //   的拷贝代价是实打实的，故这里必须一次到位。
    uLongf bound = compressBound((uLong)n);
    QByteArray out;
    out.resize((int)bound);

    uLongf destLen = (uLongf)out.size();
    const int rc = compress2((Bytef*)out.data(), &destLen,
                             (const Bytef*)raw.data(), (uLong)n, level);
    if (rc != Z_OK)
        return QByteArray();               // 失败：空串，由调用方走兜底分支
    out.resize((int)destLen);              // 截到实际长度（压缩后通常远小于上界）
    return out;
}

#endif // QT3_BUILD

#endif // QLSTIK_QZLIB_SHIM_H
