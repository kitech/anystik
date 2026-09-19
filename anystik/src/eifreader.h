#ifndef EIF_READER_H
#define EIF_READER_H

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

// QQ 表情包 .eif（OLE 复合文档 CFB）解析。
// 格式：Root 下含 Face.dat（XOR 混淆的 分组\文件名 索引）与若干分组 Storage，
// 分组内为「混淆文件名」的图片流 + 同名的 fix.bmp 缩略掩膜。
// 算法参考 readme9txt/QQEIF-Extractor（公开逆向分析 bilibili cv36661612），
// 仅按算法实现，不复抄任何 GPL 源码。
namespace eifreader {

// 前 8 字节是否 CFB 魔数 D0 CF 11 E0 A1 B1 1A E1
bool isEifFile(const QByteArray& head);

// 解码 Face.dat 得到 分组 -> QHash(混淆文件名 -> 组内序号 0..n-1)
QHash<QString, QHash<QString, int>> decodeFaceDat(const QByteArray& dat,
                                                  QString* errOut = nullptr);

// 解包 .eif 到 outDir（顶层只落「分组目录」，分组内含按序号命名 0000.<ext> 的图片，
// 跳过 fix/tmp/tmb 掩膜与 Face.dat/Face2.dat）。成功返回 true。
// 无任何图片成功也返回 true（err 给出提示）；格式/IO 错误返回 false。
bool extractEif(const QString& eifPath,
                const QString& outDir,
                QString* errOut = nullptr,
                int* imageCount = nullptr);

// 供自检：把压缩/混淆的叙述性统计写入 *out
QString describeEif(const QString& eifPath, QString* errOut = nullptr);

}  // namespace eifreader

#endif  // EIF_READER_H