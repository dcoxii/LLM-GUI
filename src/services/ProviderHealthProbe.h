#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace llm_gui::services {

struct ProviderHealth
{
    QString providerId;
    bool reachable { false };
    QString detail;
};

class ProviderHealthProbe : public QObject
{
    Q_OBJECT

public:
    explicit ProviderHealthProbe(QObject *parent = nullptr);

    void probeDeepSeek(const QString &baseUrl, const QString &apiKey);
    void probeLlamaCpp(const QString &baseUrl);

signals:
    void healthUpdated(const llm_gui::services::ProviderHealth &health);

private:
    void beginProbe(const QString &providerId,
                    const QString &url,
                    const QByteArray &authHeader = QByteArray());

    QNetworkAccessManager *m_manager;
};

} // namespace llm_gui::services
