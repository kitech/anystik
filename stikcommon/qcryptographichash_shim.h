#ifndef QLSTIK_QCRYPTOGRAPHICHASH_SHIM_H
#define QLSTIK_QCRYPTOGRAPHICHASH_SHIM_H

// QCryptographicHash 垫片（Qt3 专用）。
//
// QCryptographicHash 随 Qt 4.1 引入，Qt3.5 无此类。stickerstore.cpp 用它生成
// 贴纸包 / 文件 / URL 的稳定 ID，全仓仅 5 处，且只用 Md5 与 Sha1 两算法：
//   · fileIdFor()        （697）  Sha1(path.toUtf8()).toHex()
//   · packIdFromTitle()  （1609） Sha1(packTitle.toUtf8()).toHex().left(12)
//   · 安装包 ID          （2240） Sha1(bytes).toHex()
//   · urlHex()           （2892） Md5(url.toUtf8()).toHex().left(16)
//   · fileMd5()          （3314） 增量：Md5 + addData() 分块喂 + result()
//
// 底层实现：
//   · md5 —— 用 qlcomp/md5.c（RFC 1321 公共领域，RSA Data Security, Inc.），
//     **不**在本目录另存副本：qlstik.pro:48 经 qlite.pri:19 已无条件编译该
//     .c，另存会撞 `MD5_*` 符号。qlstik.pro:47 已 `-I ../qlcomp`，故此处
//     `#include "md5.h"` 直接解析到它。本 shim 仅在 Qt3 分支展开，Qt6 不受影响。
//   · sha1.c / sha1.h —— Steve Reid 的 SHA-1，**100% Public Domain**，
//     上游 https://github.com/henryk/tinydnssec/blob/master/sha1.c 。
//     选它而非自写：tinydnssec / jbig2dec / chromium-smhasher 均用同一份，
//     自带 3 个 FIPS 180-1 / RFC 3174 官方向量自测块（-DTEST 可跑）。
//     全仓无既有 SHA-1，故作为新文件放在本目录，Qt3 下参与编译。
//
// ⚠ 上游两套 API 的 Final 参数顺序相反，shim 内已分别按各自签名调用：
//     MD5_Final(digest, ctx)   —— md5.h:16
//     SHA1_Final(ctx, digest)  —— sha1.h:23
//
// ⚠ Qt3 的 QByteArray 是 `typedef QMemArray<char>`（qcstring.h:105，仅 Q_QDOC
//   下才是 class），**无法加成员函数**；而 QMemArray 也没有 left()（见
//   qmemarray.h，只有 data/size/count/resize/truncate/at/find/contains/…）。
//   调用点写的是成员式 `.toHex().left(12)`，故静态 hash() 返回下面的
//   Qt3HashBytes（公开继承 QByteArray），由它补出 toHex() 与 left()，
//   这样 stickerstore.cpp 一行都不用改。Qt6 侧返回原生 QByteArray，自带
//   这两个成员，调用点无需版本分支。
//
// ⚠ toHex() 输出**小写**，与 Qt6 的 QByteArray::toHex() 一致（Qt5+ 默认小写）。
//   注意别复用 stikcommon/qba_shim.h 的 qbaToHex()——那个是**大写**，为
//   QWebdav 的 pin 比对而设计，用在这里会改变生成的 ID，属行为变更。

#if QT_VERSION < 0x040100

#include <qcstring.h>
#include <qstring.h>

#include "sha1.h"
#include "md5.h"
#include "qbytearrayview_shim.h"

// ── Qt3HashBytes：补出 toHex() / left() 的 QByteArray 派生类 ──────────────
class Qt3HashBytes : public QByteArray
{
public:
    Qt3HashBytes() : QByteArray() {}
    explicit Qt3HashBytes(int size) : QByteArray(size) {}
    Qt3HashBytes(const QByteArray& other) : QByteArray(other) {}

    // 对齐 Qt5+/Qt6 的 QByteArray::toHex()：小写、每字节两位。
    Qt3HashBytes toHex() const
    {
        Qt3HashBytes out((int)size() * 2);
        char* d = out.data();
        static const char* kHex = "0123456789abcdef";
        for (int i = 0; i < (int)size(); ++i) {
            const unsigned char b = (unsigned char)at(i);
            d[i * 2]     = kHex[b >> 4];
            d[i * 2 + 1] = kHex[b & 0x0F];
        }
        return out;
    }

    // 对齐 Qt4+ 的 QByteArray::left(uint len)：取前 len 字节（超长则全取）。
    Qt3HashBytes left(int len) const
    {
        const int n = (len < (int)size()) ? len : (int)size();
        Qt3HashBytes out(n);
        memcpy(out.data(), data(), (size_t)n);
        return out;
    }
};

// ── QCryptographicHash ─────────────────────────────────────────────────
// 对齐 Qt4+ 的公开形态：Algorithm 枚举、静态 hash()、增量 addData()、
// result()、resultHex()。枚举只列实际支持的两种（Qt4+ 完整枚举还有
// Md4/Sha224/Sha256/Sha384/Sha512/Keccak_*/Sha3_*/RipeMd160/Whirlpool）。
class QCryptographicHash
{
public:
    enum Algorithm {
        Md5,
        Sha1
    };

    explicit QCryptographicHash(Algorithm algorithm)
        : m_algorithm(algorithm)
    {
        reset();
    }

    void reset()
    {
        if (m_algorithm == Sha1) {
            SHA1_Init(&m_sha1);
        } else {
            MD5_Init(&m_md5);
        }
    }

    void addData(const QByteArray& data)
    {
        if (data.isEmpty()) return;
        const uint8_t* p = (const uint8_t*)data.data();
        const size_t n = (size_t)data.size();
        if (m_algorithm == Sha1) {
            SHA1_Update(&m_sha1, p, n);
        } else {
            MD5_Update(&m_md5, p, n);
        }
    }

    // stickerstore.cpp:3317 走的形态：QByteArrayView(buf.constData(), int(got))。
    void addData(QByteArrayView view)
    {
        if (view.isEmpty()) return;
        const uint8_t* p = (const uint8_t*)view.constData();
        const size_t n = (size_t)view.size();
        if (m_algorithm == Sha1) {
            SHA1_Update(&m_sha1, p, n);
        } else {
            MD5_Update(&m_md5, p, n);
        }
    }

    QByteArray result()
    {
        // 拷贝一份 ctx 再 Final，不改调用方状态（Qt6 同语义）。
        if (m_algorithm == Sha1) {
            SHA1_CTX c = m_sha1;
            uint8_t d[SHA1_DIGEST_SIZE];
            SHA1_Final(&c, d);            // 注意：sha1 是 (ctx, digest)
            QByteArray r(SHA1_DIGEST_SIZE);
            memcpy(r.data(), d, SHA1_DIGEST_SIZE);
            return r;
        }
        MD5_CTX c = m_md5;
        uint8_t d[16];
        MD5_Final(d, &c);                 // 注意：md5 是 (digest, ctx)
        QByteArray r(16);
        memcpy(r.data(), d, 16);
        return r;
    }

    QByteArray resultHex()
    {
        return Qt3HashBytes(result()).toHex();
    }

    // 静态便捷入口。返回 Qt3HashBytes 而非 QByteArray，好让调用点能直接
    // 链式调 .toHex() / .left(n)（QMemArray 两者皆无）。
    static Qt3HashBytes hash(const QByteArray& data, Algorithm algorithm)
    {
        QCryptographicHash h(algorithm);
        h.addData(data);
        return Qt3HashBytes(h.result());
    }

private:
    Algorithm m_algorithm;
    SHA1_CTX m_sha1;
    MD5_CTX m_md5;
};

#endif // QT_VERSION < 0x040100
#endif // QLSTIK_QCRYPTOGRAPHICHASH_SHIM_H