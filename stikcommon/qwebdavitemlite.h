// QWebdavItemLite —— davbisync 实际消费的目录条目窄切面。
//
// 与 vendor qwebdav/qwebdavitem.h 的差异（都是**实测对齐 davbisync 用法**后的取舍，
// 不是漏做；消费面见 davbisync.cpp:377-415）：
//   1. 只保留 4 个访问器：path() / isDir() / size() / lastModified()。
//      vendor 另有 name()/ext()/lastModifiedStr() 及 7 个扩展访问器
//      （displayName/createdAt/contentLanguage/entityTag/mimeType/isExecutable/source），
//      davbisync 一处都没读。
//   2. **丢弃** vendor 的 operator<（qwebdavitem.cpp:201-211，依赖 QNaturalSort）以及
//      dirparser 里的 std::sort（qwebdavdirparser.cpp:333）。理由：davbisync 把
//      getList() 结果遍历进 QMap（davbisync.cpp:400-401），顺序无关。
//   3. 不带 m_ext / m_lastModifiedStr：ext 仅供 operator<，lastModifiedStr 无消费者。
//   4. 不需要 moc：无 signal/slot/元对象用法。
#ifndef QWEBDAVITEMLITE_H
#define QWEBDAVITEMLITE_H

#include <qstring.h>
#include <qdatetime.h>
#include "qglobaltype_shim.h"

class QWebdavItemLite
{
public:
    QWebdavItemLite()
        : m_dirOrFile(false), m_size(0)
    {
    }

    QWebdavItemLite(const QString& path, bool dirOrFile,
                    const QDateTime& lastModified, quint64 size)
        : m_path(path), m_dirOrFile(dirOrFile), m_lastModified(lastModified),
          m_size(size)
    {
    }

    // 相对 rootPath 的路径（dirparser 解析时已剥离）。目录项以 '/' 结尾。
    QString path() const { return m_path; }
    bool isDir() const { return m_dirOrFile; }
    // 无效 QDateTime 时原样返回，调用方 toMSecsSinceEpoch() 得 0，
    // davbisync 据此判定「服务器不支持 getlastmodified」（davbisync.cpp:396-398）。
    QDateTime lastModified() const { return m_lastModified; }
    quint64 size() const { return m_size; }

private:
    QString m_path;
    bool m_dirOrFile;
    QDateTime m_lastModified;
    quint64 m_size;
};

#endif