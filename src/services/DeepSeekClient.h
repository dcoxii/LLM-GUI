#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>

#include "services/ChatStreamParser.h"
#include "services/ProviderClient.h"

namespace llm_gui::services {

class DeepSeekClient : public ProviderClient
{
    Q_OBJECT

public:
    explicit DeepSeekClient(QObject *parent = nullptr);

    QString providerId() const override;
    QString displayName() const override;

    virtual void setBaseUrl(const QString &baseUrl);
    virtual QString baseUrl() const;

    void setApiKey(const QString &apiKey);
    QString apiKey() const;

    QString model() const override;
    void setModel(const QString &model) override;

    void setRequestTimeoutMs(int timeoutMs);

    virtual QStringList fetchAvailableModelsSync(QString *errorMessage = nullptr);

    void sendPrompt(const QString &prompt, bool stream) override;
    bool sendPromptSync(const QString &prompt, QString *response, QString *errorMessage) override;
    void cancel() override;

protected:
    virtual QString normalizedBaseUrl() const;
    virtual bool requiresApiKey() const;
    virtual QByteArray authorizationHeader() const;

private:
    void handleFinished();
    void handleReadyRead();

    QNetworkAccessManager m_manager;
    QNetworkReply *m_reply { nullptr };
    ChatStreamParser m_parser;
    QString m_baseUrl;
    QString m_apiKey;
    QString m_model;
    int m_requestTimeoutMs { 90000 };
    bool m_streamMode { false };
    bool m_finishedEmitted { false };
    bool m_failedEmitted { false };
};

} // namespace llm_gui::services
