#include "qcabundle.h"

#include "qglobaltype_shim.h"   // qMkdir（Qt3 递归建目录）

#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qstringlist.h>
#include <qtextstream.h>

#include <cstdlib>

namespace {

// 系统根证书候选路径（按优先级）。覆盖 debian/arch/fedora/suse/openssl 自建几种布局。
const char* const kSystemCaPaths[] = {
    "/etc/ssl/certs/ca-certificates.crt",   // debian / ubuntu / arch
    "/etc/pki/tls/certs/ca-bundle.crt",     // fedora / rhel
    "/etc/ssl/ca-bundle.pem",               // suse
    "/etc/pki/tls/cacert.pem",              // openssl 自建
    "/etc/ssl/cert.pem",                    // 旧 openssl
    0
};

QString configDir()
{
    const char* xdg = ::getenv("XDG_CONFIG_HOME");
    if (xdg != 0 && *xdg != '\0') {
        return QString(xdg) + "/qlstik";
    }
    const char* home = ::getenv("HOME");
    if (home != 0 && *home != '\0') {
        return QString(home) + "/.config/qlstik";
    }
    return QString();
}

QString findSystemCa()
{
    for (int i = 0; kSystemCaPaths[i] != 0; ++i) {
        const QString p(kSystemCaPaths[i]);
        if (QFileInfo(p).isFile()) {
            return p;
        }
    }
    return QString();
}

bool readWholeFile(const QString& path, QString& out)
{
    QFile f(path);
    if (!f.open(IO_ReadOnly | IO_Raw)) {
        return false;
    }
    const QByteArray data = f.readAll();
    f.close();
    out = QString::fromLatin1(data);
    return true;
}

// 证书去重用：压掉所有空白差异，只留 PEM 文本行。
QString normalizePem(const QString& pem)
{
    QString out;
    const QStringList lines = QStringList::split(QChar('\n'), pem);
    for (QStringList::const_iterator it = lines.begin(); it != lines.end(); ++it) {
        const QString line = (*it).stripWhiteSpace();
        if (!line.isEmpty()) {
            out += line;
            out += QChar('\n');
        }
    }
    return out;
}

// appendToBundle：把 extra 追加到 path 指向的文件（缺系统根证书时先铺一遍）。
// sysCa 为空表示"文件已存在且已含系统根证书"。
bool writeBundle(const QString& path, const QString& sysCa, const QString& extra)
{
    if (path.isEmpty()) {
        return false;
    }
    const QFileInfo fi(path);
    if (!qMkdir(fi.dirPath())) {
        return false;
    }
    QFile f(path);
    if (!f.open(IO_WriteOnly | IO_Truncate)) {
        return false;
    }
    QTextStream out(&f);
    if (!sysCa.isEmpty()) {
        out << sysCa;
        if (!sysCa.endsWith(QChar('\n'))) {
            out << "\n";
        }
    }
    if (!extra.isEmpty()) {
        out << extra;
        if (!extra.endsWith(QChar('\n'))) {
            out << "\n";
        }
    }
    f.flush();
    f.close();
    return true;
}

} // namespace

QString qCaBundlePath()
{
    const QString dir = configDir();
    if (dir.isEmpty()) {
        return QString();
    }
    return dir + "/dav-ca.pem";
}

bool qCaBundleTrust(const QString& certPem)
{
    const QString path = qCaBundlePath();
    if (path.isEmpty()) {
        return false;
    }
    const QString needle = normalizePem(certPem);
    if (needle.isEmpty()) {
        return false;
    }

    QString existing;
    if (readWholeFile(path, existing) && !existing.isEmpty()) {
        if (existing.contains(needle)) {
            return true;   // 已收录（幂等）
        }
        QString appended = existing;
        if (!appended.endsWith(QChar('\n'))) {
            appended += QChar('\n');
        }
        appended += needle;
        // 系统根证书已随首次写入铺好，这里 sysCa 传空避免重复。
        return writeBundle(path, QString(), appended);
    }

    // 首次：系统根证书 + 新证书
    QString sysCa;
    if (!readWholeFile(findSystemCa(), sysCa)) {
        sysCa = QString();
    }
    return writeBundle(path, sysCa, needle);
}

bool qCaBundleActivate()
{
    const QString path = qCaBundlePath();
    if (path.isEmpty()) {
        return false;
    }
    if (!QFileInfo(path).isFile()) {
        QString sysCa;
        if (!readWholeFile(findSystemCa(), sysCa)) {
            return false;   // 无系统根证书可写且 bundle 未建：不能安全激活
        }
        if (!writeBundle(path, sysCa, QString())) {
            return false;
        }
    }
    const QByteArray pathBytes = path.local8Bit();
    if (::setenv("CURL_CA_BUNDLE", pathBytes.data(), 1) != 0) {
        return false;
    }
    return true;
}

bool qCaBundleIsActive()
{
    const char* v = ::getenv("CURL_CA_BUNDLE");
    return (v != 0 && *v != '\0');
}
