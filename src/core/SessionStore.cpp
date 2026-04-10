#include "core/SessionStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace llm_gui::core {

SessionStore::SessionStore(const QString &basePath)
    : m_basePath(basePath)
{
}

QString SessionStore::basePath() const
{
    return m_basePath;
}

void SessionStore::setBasePath(const QString &basePath)
{
    m_basePath = basePath.trimmed();
}

bool SessionStore::ensureBasePath(QString *errorMessage) const
{
    QDir dir;
    if (dir.mkpath(m_basePath)) {
        return true;
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("Unable to create session directory: %1").arg(m_basePath);
    }
    return false;
}

QString SessionStore::sessionFilePath(const QString &sessionId) const
{
    return QDir(m_basePath).filePath(sessionId + ".json");
}

QVector<ChatSession> SessionStore::listSessions() const
{
    QVector<ChatSession> sessions;
    QString error;
    if (!ensureBasePath(&error)) {
        return sessions;
    }

    QDir dir(m_basePath);
    const QFileInfoList entries = dir.entryInfoList(QStringList() << "*.json", QDir::Files, QDir::Time);

    for (const QFileInfo &entry : entries) {
        QFile file(entry.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }

        QString parseError;
        ChatSession session = sessionFromJson(file.readAll(), &parseError);
        if (!session.id.isEmpty()) {
            sessions.push_back(session);
        }
    }

    return sessions;
}

bool SessionStore::saveSession(ChatSession &session, QString *errorMessage) const
{
    if (!ensureBasePath(errorMessage)) {
        return false;
    }

    session.updatedAt = QDateTime::currentDateTimeUtc();
    if (session.title.trimmed().isEmpty() || session.title == "New Chat") {
        session.title = session.suggestedTitle();
    }

    QFile file(sessionFilePath(session.id));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to write session file: %1").arg(file.fileName());
        }
        return false;
    }

    file.write(sessionToJson(session));
    return true;
}

bool SessionStore::loadSession(const QString &sessionId, ChatSession *session, QString *errorMessage) const
{
    if (!session) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Null session output pointer.");
        }
        return false;
    }

    QFile file(sessionFilePath(sessionId));
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to open session file: %1").arg(file.fileName());
        }
        return false;
    }

    *session = sessionFromJson(file.readAll(), errorMessage);
    return !session->id.isEmpty();
}

bool SessionStore::deleteSession(const QString &sessionId, QString *errorMessage) const
{
    QFile file(sessionFilePath(sessionId));
    if (!file.exists()) {
        return true;
    }

    if (!file.remove()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to delete session file: %1").arg(file.fileName());
        }
        return false;
    }

    return true;
}

ChatSession SessionStore::sessionFromJson(const QByteArray &data, QString *errorMessage) const
{
    ChatSession session;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = parseError.errorString();
        }
        return session;
    }

    const QJsonObject root = document.object();
    session.id = root.value("id").toString();
    session.title = root.value("title").toString();
    session.provider = root.value("provider").toString();
    session.model = root.value("model").toString();
    session.createdAt = QDateTime::fromString(root.value("created_at").toString(), Qt::ISODate);
    session.updatedAt = QDateTime::fromString(root.value("updated_at").toString(), Qt::ISODate);

    const QJsonArray messages = root.value("messages").toArray();
    for (const QJsonValue &value : messages) {
        const QJsonObject obj = value.toObject();
        ChatMessage message;
        message.role = obj.value("role").toString();
        message.content = obj.value("content").toString();
        message.timestamp = QDateTime::fromString(obj.value("timestamp").toString(), Qt::ISODate);
        session.messages.push_back(message);
    }

    return session;
}

QByteArray SessionStore::sessionToJson(const ChatSession &session) const
{
    QJsonArray messages;
    for (const ChatMessage &message : session.messages) {
        messages.append(QJsonObject{
            {"role", message.role},
            {"content", message.content},
            {"timestamp", message.timestamp.toUTC().toString(Qt::ISODate)}
        });
    }

    const QJsonObject root{
        {"id", session.id},
        {"title", session.title},
        {"provider", session.provider},
        {"model", session.model},
        {"created_at", session.createdAt.toUTC().toString(Qt::ISODate)},
        {"updated_at", session.updatedAt.toUTC().toString(Qt::ISODate)},
        {"messages", messages}
    };

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

} // namespace llm_gui::core
