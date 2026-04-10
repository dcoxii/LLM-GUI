#include "services/ProviderHealthProbe.h"

#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace llm_gui::services {

ProviderHealthProbe::ProviderHealthProbe(QObject *parent)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
{
}

void ProviderHealthProbe::probeDeepSeek(const QString &baseUrl, const QString &apiKey)
{
    QString endpoint = baseUrl.trimmed();
    if (endpoint.isEmpty()) {
        endpoint = QStringLiteral("https://api.openai.com/v1");
    }
    if (!endpoint.endsWith('/')) {
        endpoint += '/';
    }
    endpoint += QStringLiteral("models");

    QByteArray auth;
    if (!apiKey.trimmed().isEmpty()) {
        auth = QByteArray("Bearer ") + apiKey.toUtf8();
    }
    beginProbe(QStringLiteral("chatgpt"), endpoint, auth);
}

void ProviderHealthProbe::probeLlamaCpp(const QString &modelPath)
{
    ProviderHealth health;
    health.providerId = QStringLiteral("llama_cpp");
    const QFileInfo info(modelPath.trimmed());
    if (info.exists() && info.isFile() && info.isReadable()) {
        health.reachable = true;
        health.detail = QStringLiteral("Model ready: %1").arg(info.fileName());
    } else if (!modelPath.trimmed().isEmpty()) {
        health.reachable = false;
        health.detail = QStringLiteral("GGUF model not found: %1").arg(modelPath.trimmed());
    } else {
        health.reachable = false;
        health.detail = QStringLiteral("GGUF model not configured");
    }
    emit healthUpdated(health);
}

void ProviderHealthProbe::beginProbe(const QString &providerId,
                                     const QString &url,
                                     const QByteArray &authHeader)
{
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(3000);
    if (!authHeader.isEmpty()) {
        request.setRawHeader("Authorization", authHeader);
    }

    QNetworkReply *reply = m_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, providerId]() {
        ProviderHealth health;
        health.providerId = providerId;

        if (reply->error() == QNetworkReply::NoError) {
            health.reachable = true;
            health.detail = QStringLiteral("Reachable");
        } else {
            health.reachable = false;
            health.detail = reply->errorString();
        }

        emit healthUpdated(health);
        reply->deleteLater();
    });
}

} // namespace llm_gui::services
