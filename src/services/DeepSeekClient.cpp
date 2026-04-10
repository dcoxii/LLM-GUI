#include "services/DeepSeekClient.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkRequest>
#include <QStringList>
#include <QUrl>
#include <QVariant>

namespace llm_gui::services {

namespace {

QString responseEndpoint(const QString &baseUrl)
{
    return baseUrl + QStringLiteral("/responses");
}

QJsonObject buildResponsesPayload(const QString &model, const QString &prompt, bool stream)
{
    return QJsonObject{
        {QStringLiteral("model"), model},
        {QStringLiteral("stream"), stream},
        {QStringLiteral("input"), prompt},
        {QStringLiteral("text"), QJsonObject{
            {QStringLiteral("format"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("text")}
            }}
        }}
    };
}

QString extractTextParts(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }

    if (!value.isArray()) {
        return {};
    }

    QStringList parts;
    for (const QJsonValue &entry : value.toArray()) {
        if (!entry.isObject()) {
            continue;
        }

        const QJsonObject object = entry.toObject();
        const QString type = object.value(QStringLiteral("type")).toString();

        QString text;
        if (type == QStringLiteral("output_text") || type == QStringLiteral("text")) {
            text = object.value(QStringLiteral("text")).toString();
        } else if (type == QStringLiteral("refusal")) {
            text = object.value(QStringLiteral("refusal")).toString();
        }

        if (!text.isEmpty()) {
            parts << text;
        }
    }

    return parts.join(QString());
}

QString extractResponseText(const QJsonObject &responseObject)
{
    const QString directOutputText = responseObject.value(QStringLiteral("output_text")).toString();
    if (!directOutputText.isEmpty()) {
        return directOutputText;
    }

    const QJsonArray output = responseObject.value(QStringLiteral("output")).toArray();
    QStringList collected;
    for (const QJsonValue &itemValue : output) {
        const QJsonObject item = itemValue.toObject();
        if (item.value(QStringLiteral("type")).toString() != QStringLiteral("message")) {
            continue;
        }
        if (item.value(QStringLiteral("role")).toString() != QStringLiteral("assistant")) {
            continue;
        }

        const QString text = extractTextParts(item.value(QStringLiteral("content")));
        if (!text.isEmpty()) {
            collected << text;
        }
    }

    return collected.join(QStringLiteral("\n\n"));
}

QString extractJsonErrorMessage(const QJsonObject &object)
{
    const QJsonValue errorValue = object.value(QStringLiteral("error"));
    if (errorValue.isObject()) {
        const QJsonObject errorObject = errorValue.toObject();
        const QString message = errorObject.value(QStringLiteral("message")).toString().trimmed();
        const QString type = errorObject.value(QStringLiteral("type")).toString().trimmed();
        const QString code = errorObject.value(QStringLiteral("code")).toString().trimmed();

        QStringList parts;
        if (!message.isEmpty()) {
            parts << message;
        }
        if (!type.isEmpty()) {
            parts << QStringLiteral("type=%1").arg(type);
        }
        if (!code.isEmpty()) {
            parts << QStringLiteral("code=%1").arg(code);
        }
        if (!parts.isEmpty()) {
            return parts.join(QStringLiteral(" | "));
        }
    } else if (errorValue.isString()) {
        const QString message = errorValue.toString().trimmed();
        if (!message.isEmpty()) {
            return message;
        }
    }

    const QString message = object.value(QStringLiteral("message")).toString().trimmed();
    if (!message.isEmpty()) {
        return message;
    }

    return {};
}

QString extractErrorMessageFromBody(const QByteArray &body)
{
    const QByteArray trimmedBody = body.trimmed();
    if (trimmedBody.isEmpty()) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(trimmedBody, &parseError);
    if (parseError.error == QJsonParseError::NoError) {
        if (document.isObject()) {
            const QJsonObject root = document.object();

            QString message = extractJsonErrorMessage(root);
            if (!message.isEmpty()) {
                return message;
            }

            const QJsonObject responseObject = root.value(QStringLiteral("response")).toObject();
            message = extractJsonErrorMessage(responseObject);
            if (!message.isEmpty()) {
                return message;
            }
        }
    }

    QString message = QString::fromUtf8(trimmedBody);
    message = message.simplified();
    if (message.size() > 600) {
        message.truncate(600);
        message += QStringLiteral("…");
    }
    return message;
}

QString formatReplyError(QNetworkReply *reply, const QByteArray &body, const QString &fallback)
{
    QStringList parts;

    const QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const QVariant reasonPhrase = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute);
    if (statusCode.isValid()) {
        QString status = QStringLiteral("HTTP %1").arg(statusCode.toInt());
        const QString reason = reasonPhrase.toString().trimmed();
        if (!reason.isEmpty()) {
            status += QStringLiteral(" %1").arg(reason);
        }
        parts << status;
    }

    const QString bodyMessage = extractErrorMessageFromBody(body);
    if (!bodyMessage.isEmpty()) {
        parts << bodyMessage;
    } else {
        const QString replyMessage = reply->errorString().trimmed();
        if (!replyMessage.isEmpty()) {
            parts << replyMessage;
        }
    }

    if (parts.isEmpty()) {
        return fallback;
    }
    return parts.join(QStringLiteral(" — "));
}

bool processResponsesStreamLine(const QString &rawLine,
                                QStringList *tokens,
                                bool *finished,
                                QString *failureMessage)
{
    QString line = rawLine.trimmed();
    if (!line.startsWith(QStringLiteral("data:"))) {
        return false;
    }

    line.remove(0, 5);
    line = line.trimmed();
    if (line.isEmpty() || line == QStringLiteral("[DONE]")) {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    const QJsonObject object = document.object();
    const QString type = object.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("response.output_text.delta")) {
        const QString delta = object.value(QStringLiteral("delta")).toString();
        if (!delta.isEmpty() && tokens) {
            tokens->append(delta);
        }
        return true;
    }

    if (type == QStringLiteral("response.output_text.done")) {
        return true;
    }

    if (type == QStringLiteral("response.failed")) {
        if (failureMessage) {
            *failureMessage = extractJsonErrorMessage(object.value(QStringLiteral("response")).toObject());
            if (failureMessage->isEmpty()) {
                *failureMessage = QStringLiteral("OpenAI response failed.");
            }
        }
        return true;
    }

    if (type == QStringLiteral("response.completed")) {
        if (finished) {
            *finished = true;
        }
        return true;
    }

    return false;
}

} // namespace

DeepSeekClient::DeepSeekClient(QObject *parent)
    : ProviderClient(parent)
    , m_baseUrl(QStringLiteral("https://api.openai.com/v1"))
    , m_model(QStringLiteral("gpt-4.1-mini"))
{
}

QString DeepSeekClient::providerId() const
{
    return QStringLiteral("chatgpt");
}

QString DeepSeekClient::displayName() const
{
    return QStringLiteral("ChatGPT");
}

void DeepSeekClient::setBaseUrl(const QString &baseUrl)
{
    m_baseUrl = baseUrl.trimmed();
}

QString DeepSeekClient::baseUrl() const
{
    return m_baseUrl;
}

void DeepSeekClient::setApiKey(const QString &apiKey)
{
    m_apiKey = apiKey.trimmed();
}

QString DeepSeekClient::apiKey() const
{
    return m_apiKey;
}

QString DeepSeekClient::model() const
{
    return m_model;
}

void DeepSeekClient::setModel(const QString &model)
{
    m_model = model.trimmed();
}

void DeepSeekClient::setRequestTimeoutMs(int timeoutMs)
{
    m_requestTimeoutMs = timeoutMs;
}

QString DeepSeekClient::normalizedBaseUrl() const
{
    QString endpoint = m_baseUrl.trimmed();
    if (endpoint.isEmpty()) {
        endpoint = QStringLiteral("https://api.openai.com/v1");
    }
    if (endpoint.endsWith('/')) {
        endpoint.chop(1);
    }
    return endpoint;
}

bool DeepSeekClient::requiresApiKey() const
{
    return true;
}

QByteArray DeepSeekClient::authorizationHeader() const
{
    if (m_apiKey.trimmed().isEmpty()) {
        return {};
    }
    return QByteArray("Bearer ") + m_apiKey.toUtf8();
}

QStringList DeepSeekClient::fetchAvailableModelsSync(QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }

    if (requiresApiKey() && m_apiKey.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("OpenAI API key is not configured.");
        }
        return {};
    }

    QNetworkRequest request{QUrl(normalizedBaseUrl() + QStringLiteral("/models"))};
    request.setTransferTimeout(m_requestTimeoutMs);
    const QByteArray authHeader = authorizationHeader();
    if (!authHeader.isEmpty()) {
        request.setRawHeader("Authorization", authHeader);
    }

    QNetworkReply *reply = m_manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray body = reply->readAll();

    if (reply->error() != QNetworkReply::NoError) {
        if (errorMessage) {
            *errorMessage = formatReplyError(reply, body, QStringLiteral("Unable to fetch OpenAI models."));
        }
        reply->deleteLater();
        return {};
    }

    reply->deleteLater();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to parse OpenAI models response.");
        }
        return {};
    }

    QStringList models;
    for (const QJsonValue &entry : document.object().value(QStringLiteral("data")).toArray()) {
        const QString id = entry.toObject().value(QStringLiteral("id")).toString().trimmed();
        if (!id.isEmpty()) {
            models << id;
        }
    }

    models.removeDuplicates();
    models.sort();
    return models;
}

void DeepSeekClient::sendPrompt(const QString &prompt, bool stream)
{
    if (prompt.trimmed().isEmpty()) {
        emit requestFailed(QStringLiteral("Prompt is empty."));
        return;
    }

    if (requiresApiKey() && m_apiKey.trimmed().isEmpty()) {
        emit requestFailed(QStringLiteral("OpenAI API key is not configured."));
        return;
    }

    cancel();
    m_parser = ChatStreamParser();
    m_finishedEmitted = false;
    m_failedEmitted = false;
    m_streamMode = stream;

    QNetworkRequest request{QUrl(responseEndpoint(normalizedBaseUrl()))};
    request.setTransferTimeout(m_requestTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const QByteArray authHeader = authorizationHeader();
    if (!authHeader.isEmpty()) {
        request.setRawHeader("Authorization", authHeader);
    }
    if (m_streamMode) {
        request.setRawHeader("Accept", "text/event-stream");
    }

    const QJsonObject payload = buildResponsesPayload(m_model, prompt, m_streamMode);

    emit statusChanged(QStringLiteral("Connecting to %1").arg(request.url().toString()));
    m_reply = m_manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    connect(m_reply, &QNetworkReply::finished, this, &DeepSeekClient::handleFinished);
    connect(m_reply, &QNetworkReply::readyRead, this, &DeepSeekClient::handleReadyRead);
    connect(m_reply, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError) {
        if (!m_failedEmitted && m_reply && m_reply->error() != QNetworkReply::OperationCanceledError) {
            emit statusChanged(QStringLiteral("Waiting for OpenAI error details..."));
        }
    });

    emit streamStarted();
}

bool DeepSeekClient::sendPromptSync(const QString &prompt, QString *response, QString *errorMessage)
{
    if (response) {
        response->clear();
    }
    if (errorMessage) {
        errorMessage->clear();
    }

    if (prompt.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Prompt is empty.");
        }
        return false;
    }

    if (requiresApiKey() && m_apiKey.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("OpenAI API key is not configured.");
        }
        return false;
    }

    QNetworkRequest request{QUrl(responseEndpoint(normalizedBaseUrl()))};
    request.setTransferTimeout(m_requestTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const QByteArray authHeader = authorizationHeader();
    if (!authHeader.isEmpty()) {
        request.setRawHeader("Authorization", authHeader);
    }

    const QJsonObject payload = buildResponsesPayload(m_model, prompt, false);

    emit statusChanged(QStringLiteral("Connecting to %1").arg(request.url().toString()));
    QNetworkReply *reply = m_manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray body = reply->readAll();

    if (reply->error() != QNetworkReply::NoError) {
        if (errorMessage) {
            *errorMessage = formatReplyError(reply, body, QStringLiteral("OpenAI request failed."));
        }
        reply->deleteLater();
        emit statusChanged(QStringLiteral("Ready"));
        return false;
    }

    reply->deleteLater();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to parse OpenAI response.");
        }
        emit statusChanged(QStringLiteral("Ready"));
        return false;
    }

    const QString text = extractResponseText(document.object());
    if (text.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("OpenAI response did not include assistant text.");
        }
        emit statusChanged(QStringLiteral("Ready"));
        return false;
    }

    if (response) {
        *response = text;
    }
    emit statusChanged(QStringLiteral("Ready"));
    return true;
}

void DeepSeekClient::cancel()
{
    if (!m_reply) {
        return;
    }

    disconnect(m_reply, nullptr, this, nullptr);
    if (m_reply->isRunning()) {
        m_reply->abort();
    }
    m_reply->deleteLater();
    m_reply = nullptr;
    emit statusChanged(QStringLiteral("Ready"));
}

void DeepSeekClient::handleReadyRead()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply || reply != m_reply || !m_streamMode) {
        return;
    }

    const QByteArray chunk = reply->readAll();
    const QStringList lines = m_parser.push(chunk);

    for (const QString &line : lines) {
        QStringList tokens;
        bool finished = false;
        QString failureMessage;

        processResponsesStreamLine(line, &tokens, &finished, &failureMessage);

        for (const QString &token : tokens) {
            emit tokenReceived(token);
        }

        if (!failureMessage.isEmpty() && !m_failedEmitted) {
            m_failedEmitted = true;
            emit requestFailed(failureMessage);
            emit statusChanged(QStringLiteral("OpenAI request failed"));
        }

        if (finished && !m_finishedEmitted) {
            m_finishedEmitted = true;
            emit streamFinished();
        }
    }
}

void DeepSeekClient::handleFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply || reply != m_reply) {
        return;
    }

    const QByteArray body = reply->readAll();

    if (reply->error() != QNetworkReply::NoError &&
        reply->error() != QNetworkReply::OperationCanceledError) {
        if (!m_failedEmitted) {
            m_failedEmitted = true;
            emit requestFailed(formatReplyError(reply, body, QStringLiteral("OpenAI request failed.")));
            emit statusChanged(QStringLiteral("OpenAI request failed"));
        }
        reply->deleteLater();
        m_reply = nullptr;
        return;
    }

    if (reply->error() == QNetworkReply::OperationCanceledError) {
        reply->deleteLater();
        m_reply = nullptr;
        emit statusChanged(QStringLiteral("Ready"));
        return;
    }

    if (m_streamMode) {
        const QString trailing = m_parser.flushRemainder().trimmed();
        if (!trailing.isEmpty()) {
            QStringList tokens;
            bool finished = false;
            QString failureMessage;
            processResponsesStreamLine(trailing, &tokens, &finished, &failureMessage);

            for (const QString &token : tokens) {
                emit tokenReceived(token);
            }

            if (!failureMessage.isEmpty() && !m_failedEmitted) {
                m_failedEmitted = true;
                emit requestFailed(failureMessage);
                emit statusChanged(QStringLiteral("OpenAI request failed"));
            }

            if (finished && !m_finishedEmitted) {
                m_finishedEmitted = true;
                emit streamFinished();
            }
        }

        if (!m_failedEmitted && !m_finishedEmitted) {
            m_finishedEmitted = true;
            emit streamFinished();
        }

        emit statusChanged(QStringLiteral("Ready"));
        reply->deleteLater();
        m_reply = nullptr;
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit requestFailed(QStringLiteral("Unable to parse OpenAI response."));
        emit statusChanged(QStringLiteral("OpenAI request failed"));
        reply->deleteLater();
        m_reply = nullptr;
        return;
    }

    const QString content = extractResponseText(document.object());
    if (content.isEmpty()) {
        emit requestFailed(QStringLiteral("OpenAI response did not include assistant text."));
        emit statusChanged(QStringLiteral("OpenAI request failed"));
        reply->deleteLater();
        m_reply = nullptr;
        return;
    }

    emit tokenReceived(content);
    emit streamFinished();
    emit statusChanged(QStringLiteral("Ready"));

    reply->deleteLater();
    m_reply = nullptr;
}

} // namespace llm_gui::services
