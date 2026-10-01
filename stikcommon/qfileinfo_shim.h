#ifndef QLSTIK_QFILEINFO_SHIM_H
#define QLSTIK_QFILEINFO_SHIM_H

// QFileInfo 跨版本助手。
//
// Qt3 的 QFileInfo 没有 Qt4+ 的 suffix()/completeSuffix()/baseName()（它们随
// Qt4.0 引入），只有 extension(bool)。故 stickerstore.cpp 里多处 `fi.suffix()`
// 在 Qt3 编不过。本头按项目既有「自由函数 + 调用点收口」范式（同 qToLower /
// qIndexOf）提供等价助手，调用点在两版本下写法一致。
//
// 语义等价性（Qt3.5 与 Qt6.7.3 实测）：
//     Qt6 suffix()         ≡ Qt3 extension(false)   （a.tar.gz→gz，.bashrc→bashrc，a.→空）
//     Qt6 completeSuffix() ≡ Qt3 extension(true)    （a.tar.gz→tar.gz）
// ⚠ 实测数据见本次会话临时探针 /tmp/vtest/tsfx3.cpp 与 tsfx6.cpp。

#include <qglobal.h>
#include <qfileinfo.h>
#include <qstring.h>

#if QT_VERSION < 0x040000

inline QString qFileInfoSuffix(const QFileInfo& fi)         { return fi.extension(false); }
inline QString qFileInfoCompleteSuffix(const QFileInfo& fi) { return fi.extension(true); }

#else

inline QString qFileInfoSuffix(const QFileInfo& fi)         { return fi.suffix(); }
inline QString qFileInfoCompleteSuffix(const QFileInfo& fi) { return fi.completeSuffix(); }

#endif

#endif // QLSTIK_QFILEINFO_SHIM_H