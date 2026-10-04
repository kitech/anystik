#include "phonedb.h"
#include "phoneloc.h"
#include "qstring_shim.h"   // QStringLiteral(Qt<4.1) 垫片

#ifdef QT3_BUILD
#include <qdict.h>
#include <qptrlist.h>
#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qurl.h>
#else
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#endif
#ifdef QT3_BUILD
#include "qnam_shim.h"
#include "qconnect_slots.h"
#else
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#endif

// QStandardPaths 是 Qt5 才引入（Qt3/Qt4 无头文件）：Qt4 由 qstandardpaths_shim.h
// 提供；Qt5+ 由 Qt 自带。shim 头带 #[if QT_VERSION < 0x050000] 无条件引入即可。
#if QT_VERSION < 0x050000
#include "qstandardpaths_shim.h"
#else
#include <QStandardPaths>
#endif

namespace {
// 钉死 commit：20326/packme master（2025.03，4,652,693B，与 gh-proxy 前缀惯例一致）
const char* const kPhoneDatUrl =
    "https://gh-proxy.org/https://raw.githubusercontent.com/20326/packme/"
    "d83f10390153059e2c0e4b7aa7f3f4b4c8f22574/phone.2503.dat";
const qint64 kExpectedSize = 4652693;
}

PhoneDb::PhoneDb() : QObject(nullptr)
{
    ensureData();
}

PhoneDb* PhoneDb::instance()
{
    static PhoneDb s;
    return &s;
}

QString PhoneDb::dataPath() const
{
    const QString dir = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation) + QStringLiteral("/phone");
#ifdef QT3_BUILD
    qMkdir(dir, true);            // compatcore34 的 recursive 重载，避开二义
#else
    QDir().mkpath(dir);
#endif
    return dir + QStringLiteral("/phone.dat");
}

void PhoneDb::loadFromFile(const QByteArray& body)
{
    m_data = body;
#ifdef QT3_BUILD
    const char* d = m_data.data();
#else
    const char* d = m_data.constData();
#endif
    const size_t n = phoneloc_load(d, m_data.size());
    if (n)
        setStatus(QStringLiteral("v%1 · %2 段")
            .arg(phoneloc_version(d)).arg(n));
    else
        setStatus(QStringLiteral("库解析失败"));

    m_metaText = n
        ? tr("库 v%1 · %2 段 · %3 MB · %4")
            .arg(QString::number(phoneloc_version(d)))
            .arg(n)
            .arg(QString::number(m_data.size() / 1048576.0, 'f', 2))
            .arg(dataPath())
        : QString();
}

void PhoneDb::setStatus(const QString& s)
{
    if (m_status == s)
        return;
    m_status = s;
    emit statusChanged();
}

void PhoneDb::ensureData()
{
#ifdef QT3_BUILD
    using ::connect;   // Qt3：qconnect_slots 全局模板进成员查找（§6.1）
#endif
    const QFileInfo fi(dataPath());
    if (fi.exists() && fi.size() == kExpectedSize) {
        QFile f(dataPath());
#ifdef QT3_BUILD
        if (f.open(IO_ReadOnly)) {
#else
        if (f.open(QIODevice::ReadOnly)) {
#endif
            loadFromFile(f.readAll());
            return;
        }
    }
    if (m_reply)
        return;                       // 下载进行中

    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl(QString::fromUtf8(kPhoneDatUrl)));
    req.setRawHeader("User-Agent", "anystik/1.0");
    req.setTransferTimeout(60000);
    setStatus(tr("归属地库下载中…"));
    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        const bool ok = m_reply->error() == QNetworkReply::NoError;
        const QByteArray body = ok ? m_reply->readAll() : QByteArray();
        m_reply->deleteLater();
        m_reply = nullptr;
        if (body.isEmpty()) {
            setStatus(tr("归属地库下载失败"));
            return;
        }
        if ((qint64)body.size() != kExpectedSize) {
            setStatus(tr("归属地库下载失败(大小不符)"));
            return;
        }
        QFile f(dataPath());
#ifdef QT3_BUILD
        if (f.open(IO_WriteOnly)) {
#else
        if (f.open(QIODevice::WriteOnly)) {
#endif
            #ifdef QT3_BUILD
            f.writeBlock(body.data(), body.size());
#else
            f.write(body);
#endif
            loadFromFile(body);
        }
    });
}

PhoneDb::Result PhoneDb::lookup(const QString& number) const
{
    Result r;
    if (m_data.isEmpty())
        return r;
    QString digits = number;
    // QRegularExpression 是 Qt5 才引入（Qt3/Qt4 无）；QRegExp 又被 Qt6 移除。
    // 这里只剥离非数字，用 QChar::isDigit 逐字符过滤，三端零依赖、语义等价。
    {
        QString filtered;
        for (int i = 0; i < digits.length(); ++i) {
            if (digits[i].isDigit())
                filtered += digits[i];
        }
        digits = filtered;
    }
    if (digits.length() != 11)
        return r;
    phoneloc_record rec;
#ifdef QT3_BUILD
    const QCString utf8 = digits.utf8();
    if (!phoneloc_query(utf8.data(), &rec))
#else
    const QByteArray utf8 = digits.toUtf8();
    if (!phoneloc_query(utf8.constData(), &rec))
#endif
        return r;
    r.ok = true;
    r.province     = QString::fromUtf8(rec.province);
    r.city         = QString::fromUtf8(rec.city);
    r.zip          = QString::fromUtf8(rec.zip);
    r.areaCode     = QString::fromUtf8(rec.area_code);
    r.cardType     = rec.card_type;
    r.operatorName = QString::fromUtf8(phoneloc_card_name(rec.card_type));
    return r;
}

QString PhoneDb::statusText() const { return m_status; }
QString PhoneDb::metaText() const { return m_metaText; }
bool PhoneDb::ready() const { return !m_data.isEmpty(); }