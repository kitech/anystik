#ifndef PHONEDB_H
#define PHONEDB_H

#include <QObject>
#include <QString>
#include <QByteArray>

class QNetworkAccessManager;
class QNetworkReply;

// 离线手机号码归属地库（packme v2503 / commit d83f…574，4,652,693B）
// 运行时下载到 AppLocalDataLocation/phone/phone.dat，懒加载进内存二分查询。
class PhoneDb : public QObject
{
    Q_OBJECT
public:
    struct Result {
        bool ok = false;
        QString province, city, zip, areaCode, operatorName;
        int cardType = 0;
    };

    static PhoneDb* instance();
    void ensureData();
    Result lookup(const QString& number) const;
    QString statusText() const;
    bool ready() const;

Q_SIGNALS:
    void statusChanged();

private:
    PhoneDb();
    QString dataPath() const;
    void loadFromFile(const QByteArray& body);
    void setStatus(const QString& s);

    QByteArray m_data;
    QString m_status;
    QNetworkAccessManager* m_nam = nullptr;
    QNetworkReply* m_reply = nullptr;
};

#endif