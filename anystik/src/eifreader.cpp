#include "eifreader.h"

#include "compoundfilereader.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace eifreader {

namespace {

// CFB (Compound File Binary) 魔数，OLE 复合文档头部 8 字节
constexpr uint8_t kCfbMagic[8] = {0xD0, 0xCF, 0x11, 0xE0,
                                  0xA1, 0xB1, 0x1A, 0xE1};

// Face.dat 中「分组\文件名」段落的固定标记（XOR 混淆特征串）
const QByteArray kFaceMarker()
{
    static const QByteArray bytes =
        QByteArray("\x98\xeb\x9f\xeb\x99\xeb\xad\xeb\x82\xeb\x87\xeb"
                   "\x8e\xeb\x84\xeb\x99\xeb\x8c\xeb", 20);
    return bytes;
}

// UTF-16LE（uint16 数组，native == LE）转 UTF-8 QString
QString u16ToQString(const uint16_t* chars, int count)
{
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(chars),
                              count);
}

// eif 组目录名（如 "1"、"我的表情"）文件系统安全化
QString sanitizeGroup(const QString& group)
{
    QString out = group;
    const QString dangerous = QStringLiteral("\\/:*?\"<>|");
    for (QChar& c : out) {
        if (c == QLatin1Char('\n') || c == QLatin1Char('\r')
            || dangerous.contains(c)) {
            c = QLatin1Char('_');
        }
    }
    if (out.trimmed().isEmpty()) {
        out = QStringLiteral("group");
    }
    return out;
}

}  // anonymous namespace

bool isEifFile(const QByteArray& head)
{
    return head.size() >= 8
        && memcmp(head.constData(), kCfbMagic, 8) == 0;
}

QHash<QString, QHash<QString, int>> decodeFaceDat(const QByteArray& dat,
                                                  QString* errOut)
{
    QHash<QString, QHash<QString, int>> result;

    if (dat.size() < 8) {
        if (errOut) *errOut = QStringLiteral("Face.dat 过短");
        return result;
    }

    const QByteArray marker = kFaceMarker();
    const int markerLen = marker.size();

    // Face.dat 按下标顺序逐行扫描，行间以 \n 分隔（含 \r\n 的 \r 一并 trim）
    const char* p = dat.constData();
    const char* end = p + dat.size();
    while (p < end) {
        const char* nl = static_cast<const char*>(memchr(p, '\n', end - p));
        const char* lineEnd = nl ? nl : end;
        QByteArray line(p, int(lineEnd - p));
        line = line.trimmed();
        p = lineEnd + (nl ? 1 : 0);
        if (line.isEmpty()) continue;

        const int start = line.indexOf(marker);
        if (start < 0) continue;

        // 标记之后跳过 4 字节分隔头，再找「重复三次的密钥」定位段落
        int idx = start + markerLen + 4;
        if (idx >= line.size()) continue;

        // find_key：从 idx 起找 byte == line[i+2] == line[i+4]
        int key = -1;
        int seek = 0;
        for (int i = idx; i < line.size(); ++i) {
            if (i + 4 >= line.size()) break;
            if (line.at(i) == line.at(i + 2)
                && line.at(i) == line.at(i + 4)) {
                key = static_cast<unsigned char>(line.at(i));
                seek = i;
                break;
            }
        }
        if (key < 0) continue;

        // get_part：从 seek 起步长 2 扫到第一个 != key 处为止；
        // endIx 为排他边界（等效 Python line[start_idx-1:end]）
        int endIx = 0;
        for (int i = seek; i < line.size(); i += 2) {
            if (static_cast<unsigned char>(line.at(i)) != key) {
                endIx = i - 1;
                break;
            }
        }
        if (endIx == 0) endIx = line.size() - 1;

        // XOR 解码段落（排他边界到 endIx-1，末尾 0 值字节跳过）
        QByteArray decoded;
        decoded.reserve(endIx - seek + 1);
        for (int i = seek - 1; i < endIx && i < line.size(); ++i) {
            const int c = static_cast<unsigned char>(line.at(i)) ^ key;
            if (c != 0) decoded.append(char(c));
        }
        const QString text = QString::fromUtf8(decoded);

        // 形如 "UserDataCustomFace:分组\文件名" 或 "分组\文件名"
        QString body = text;
        if (body.split(QLatin1Char(':')).size() > 1) {
            if (!body.startsWith(QStringLiteral("UserDataCustomFace:"))) {
                continue;
            }
            body = body.mid(int(qstrlen("UserDataCustomFace:")));
        }
        const QStringList parts = body.split(QLatin1Char('\\'));
        if (parts.size() < 2) continue;
        const QString group = parts.at(0);
        const QString file = parts.at(1);
        if (group.isEmpty() || file.isEmpty()) continue;

        auto& inner = result[group];
        if (!inner.contains(file)) {
            inner.insert(file, inner.size());   // 组内序号按出现次序
        }
    }

    if (errOut && result.isEmpty()) {
        *errOut = QStringLiteral("Face.dat 中未识别到任何表情条目");
    }
    return result;
}

bool extractEif(const QString& eifPath,
                const QString& outDir,
                QString* errOut,
                int* imageCount)
{
    if (imageCount) *imageCount = 0;

    QFile in(eifPath);
    if (!in.open(QIODevice::ReadOnly)) {
        if (errOut) *errOut = QStringLiteral("无法打开 eif：") + eifPath;
        return false;
    }
    const QByteArray buf = in.readAll();
    in.close();
    if (buf.size() < 512 || !isEifFile(buf)) {
        if (errOut) *errOut = QStringLiteral("不是合法 eif 文件");
        return false;
    }

    int extracted = 0;
    QString internalErr;

    try {
        CFB::CompoundFileReader cf(buf.constData(), size_t(buf.size()));

        // 1) 先遍历收集 Face.dat 与 Face2.dat（含分组索引）
        QByteArray faceDat;
        cf.EnumFiles(cf.GetRootEntry(), -1,
            [&](const CFB::COMPOUND_FILE_ENTRY* e,
                const std::u16string&, int) {
                if (internalErr.isEmpty() && cf.IsStream(e)) {
                    const int nameLen = int(e->nameLen / 2) - 1;
                    if (nameLen == int(qstrlen("Face.dat"))
                        && u16ToQString(e->name, nameLen)
                               == QLatin1String("Face.dat")) {
                        faceDat = QByteArray(int(e->size), Qt::Uninitialized);
                        cf.ReadFile(e, 0, faceDat.data(),
                                    size_t(faceDat.size()));
                    }
                }
            });

        // 2) 建立 分组名 -> (混淆明文) -> 序号
        QString faceErr;
        const auto groupMap = decodeFaceDat(faceDat, &faceErr);

        // 3) 遍历 CFB：目录为分组，其下图片流写入 outDir/<分组>/<序号>.<ext>
        cf.EnumFiles(cf.GetRootEntry(), -1,
            [&](const CFB::COMPOUND_FILE_ENTRY* e,
                const std::u16string& dir, int level) {
                if (internalErr.isEmpty() && cf.IsStream(e)) {
                    const int nameLen = int(e->nameLen / 2) - 1;
                    const QString name = u16ToQString(e->name, nameLen);
                    if (name.isEmpty()) return;

                    // Face.dat / Face2.dat 是索引流，跳过
                    if (name == QLatin1String("Face.dat")
                        || name == QLatin1String("Face2.dat")) {
                        return;
                    }

                    // 组名取该流所在目录的最后一段（CFB 的 dir 形如 "1"）。
                    // 根级流（dir 为空，如 Face.dat 之外的其他 dat）不属于任何
                    // 分组，参照读码逻辑直接跳过。
                    const QString dirStr = QString::fromStdU16String(dir);
                    if (dirStr.isEmpty()) return;
                    QString group = dirStr;
                    const int lastSep = group.lastIndexOf(QLatin1Char('\\'));
                    if (lastSep >= 0) group = group.mid(lastSep + 1);
                    group = sanitizeGroup(group);

                    const QDir target(outDir + QLatin1Char('/') + group);
                    QString base, ext;
                    const int dot = name.lastIndexOf(QLatin1Char('.'));
                    if (dot >= 0) {
                        base = name.left(dot);
                        ext = name.mid(dot + 1);
                    } else {
                        base = name;
                    }

                    // 掩膜/临时流即 `xxfix.bmp`：名字以 fix/tmp/tmb 结尾
                    const QString lowerBase = base.toLower();
                    if (lowerBase.endsWith(QLatin1String("fix"))
                        || lowerBase.endsWith(QLatin1String("tmp"))
                        || lowerBase.endsWith(QLatin1String("tmb"))) {
                        return;
                    }

                    // 组内序号：Face.dat 索引命中则用序号命名，否则回退原名
                    QString stem = base;
                    const auto gIt = groupMap.constFind(group);
                    if (gIt != groupMap.constEnd()) {
                        const auto& inner = gIt.value();
                        const auto fIt = inner.constFind(name);
                        if (fIt != inner.constEnd()) {
                            stem = QStringLiteral("%1").arg(
                                fIt.value(), 4, 10, QLatin1Char('0'));
                        }
                    }
                    const QString suffix = ext.toLower().isEmpty()
                        ? QStringLiteral("img") : ext.toLower();
                    const QString outPath = target.filePath(
                        stem + QLatin1Char('.') + suffix);

                    if (!target.exists() && !target.mkpath(
                            QStringLiteral("."))) {
                        internalErr = QStringLiteral("无法创建分组目录：")
                                      + group;
                        return;
                    }

                    QByteArray data(int(e->size), Qt::Uninitialized);
                    cf.ReadFile(e, 0, data.data(), size_t(e->size));

                    QFile of(outPath);
                    if (!of.open(QIODevice::WriteOnly)) {
                        internalErr = QStringLiteral("无法写表情文件");
                        return;
                    }
                    of.write(data);
                    of.close();
                    ++extracted;
                }
            });
    } catch (const std::exception& ex) {
        if (errOut) {
            *errOut = QStringLiteral("eif 解析失败：")
                      + QString::fromUtf8(ex.what());
        }
        return false;
    }

    if (imageCount) *imageCount = extracted;
    if (extracted == 0) {
        if (errOut && !internalErr.isEmpty()) *errOut = internalErr;
        else if (errOut) *errOut = QStringLiteral("包内无可用图片");
        return false;
    }
    return true;
}

QString describeEif(const QString& eifPath, QString* errOut)
{
    QString result;
    QFile in(eifPath);
    if (!in.open(QIODevice::ReadOnly)) {
        if (errOut) *errOut = QStringLiteral("无法打开 eif：") + eifPath;
        return result;
    }
    const QByteArray buf = in.readAll();
    in.close();
    if (!isEifFile(buf)) {
        if (errOut) *errOut = QStringLiteral("不是合法 eif 文件");
        return result;
    }
    int groups = 0, streams = 0;
    int realImages = 0;
    QStringList groupNames;
    try {
        CFB::CompoundFileReader cf(buf.constData(), size_t(buf.size()));
        cf.EnumFiles(cf.GetRootEntry(), -1,
            [&](const CFB::COMPOUND_FILE_ENTRY* e,
                const std::u16string& dir, int) {
                const bool isDir = !cf.IsStream(e);
                const int nameLen = int(e->nameLen / 2) - 1;
                const QString name = u16ToQString(e->name, nameLen);
                const auto first = dir.empty()
                    ? name : QString::fromStdU16String(dir) + QChar('\\') + name;
                if (isDir) {
                    ++groups;
                    groupNames.append(name);
                } else {
                    ++streams;
                    if (!name.endsWith(QLatin1String(".dat"))) {
                        ++realImages;
                    }
                }
                Q_UNUSED(first);
            });
    } catch (const std::exception& ex) {
        if (errOut) *errOut = QString::fromUtf8(ex.what());
        return result;
    }
    result = QStringLiteral("groups=%1 streams=%2 images=%3 dirs=%4")
                 .arg(groups).arg(streams).arg(realImages)
                 .arg(groupNames.join(QLatin1Char(',')));
    return result;
}

}  // namespace eifreader