#ifndef PUSH_HANDLER_H
#define PUSH_HANDLER_H

#include <QObject>
#include <QStringList>
#include <QVector>

enum class PushProviderType {
    UnifiedPush = 0,
    Gotify = 1,
};

class PushHandler : public QObject
{
    Q_OBJECT
public:
    static void start();
    static void stop();
    static PushHandler* instance();
    static bool isNtfyInstalled();
    static bool isConnected();
    static bool isRegistering();
    static QString upDistributorDisplayName(const QString& packageName);
    static QStringList installedDistributors();
    static QVector<QPair<QString,QString>> knownDistributors();
    static int ignoredMessageCount();
    void setConnected(bool v);
    void setRegistering(bool v);

    void registerDevice();
    void selectDistributor(const QString& distributor);
    void switchDistributor(const QString& newDistributor);
    void cancelRegistrationTimeout();

    PushProviderType providerType() const;
    QString currentDistributor() const;
    QString currentDistributorDisplayName() const;
    void setProviderType(PushProviderType type);
    void setCurrentDistributor(const QString& dist);

Q_SIGNALS:
    void distributorsFound(const QStringList& distributors);
    void distributorsUpdated(const QStringList& distributors);
    void pushReceived(const QString& endpoint, const QString& instance);
    void pushMessage(const QByteArray& message, const QString& instance);
    void registrationFailed(const QString& reason);
    void registrationSent();
    void statusChanged();

protected:
    void timerEvent(QTimerEvent* event) override;

private:
    void startRegistrationTimeout();
    int m_regTimeoutTimerId = 0;
    bool m_isConnected = false;
    bool m_isRegistering = false;
    PushProviderType m_providerType = PushProviderType::UnifiedPush;
    QString m_currentDistributor;
    QStringList m_installedDistributorsCache;
};

#endif
