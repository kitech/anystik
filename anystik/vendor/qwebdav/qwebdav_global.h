#ifndef QTWEBDAV_GLOBAL_H
#define QTWEBDAV_GLOBAL_H

#include <QtCore/qglobal.h>

/****************************************************************************
** QtWebDAV v2.1 导出头的跨平台静态并入版
**
** QtWebDAV 原用 CMake 的 generate_export_header() 在构建期生成此头
** （QtWebDAV-2.1/CMakeLists.txt: generate_export_header(QtWebDAV
**   BASE_NAME QTWEBDAV ...)），定义 QTWEBDAV_EXPORT 用于类声明。
** anystik 把 QtWebDAV 的 4 个 .cpp 直接从 vendor/qwebdav 编译进自身单一
** 可执行目标（静态一体化，不产生独立共享库），因此导出宏在默认路径下
** 恒为空——与上游"非跨平台"构建等价。
**
** 为保证日后若拆成独立共享库、或有窗外消费者在它自己的编译期链上
** vendored 头时，跨平台（Windows 的 __declspec、Linux/macOS 的
** -fvisibility 属性）行为一致，这里仍按 Qt 规范补全导入/导出推断：
**
**   构建独立库（Windows 等需要符号导出的平台）：编译期定义
**     QTWEBDAV_SHARED + QTWEBDAV_LIBRARY → QTWEBDAV_EXPORT = Q_DECL_EXPORT
**   外部消费者：仅定义 QTWEBDAV_SHARED（消费者侧）→ Q_DECL_IMPORT
**   anystik 静态并入：两者都不定义 → 宏为空
**
** 另外：开启 QWEBDAVITEM_EXTENDED_PROPERTIES / QTWEBDAVITEM_EXTENDED_
** PROPERTIES，使 QWebdavItem 解析并暴露扩展属性（entityTag→entityTag()
** 等）。增量同步引擎（davbisync）依赖远端 etag 做增量对比，故此开关必须
** 为真；因为静态并入，无 ABI 顾虑，两种拼写都定义以容错 vendored 头的
** 实际守卫写法。
****************************************************************************/

/* 跨平台导出宏：anystik 静态并入 → 空（无独立共享库）。
 * 若日后拆库：构建期定义 QTWEBDAV_SHARED + QTWEBDAV_LIBRARY。   */
#if defined(QTWEBDAV_SHARED)
#  if defined(QTWEBDAV_LIBRARY)
#    define QTWEBDAV_EXPORT Q_DECL_EXPORT
#  else
#    define QTWEBDAV_EXPORT Q_DECL_IMPORT
#  endif
#else
#  define QTWEBDAV_EXPORT
#endif

/* 兼容别名：某些 vendored 头（如 qwebdavdirparser.h）用 QWEBDAV_EXPORT。 */
#define QWEBDAV_EXPORT QTWEBDAV_EXPORT

/* 扩展属性（entityTag/entityTag()）：双拼写全定义，容错任一字面。 */
#define QWEBDAVITEM_EXTENDED_PROPERTIES
#define QTWEBDAVITEM_EXTENDED_PROPERTIES

#endif // QTWEBDAV_GLOBAL_H
