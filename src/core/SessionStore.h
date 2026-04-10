#pragma once

#include <QString>
#include <QVector>

#include "core/ChatSession.h"

namespace llm_gui::core {

class SessionStore
{
public:
    explicit SessionStore(const QString &basePath);

    QString basePath() const;
    void setBasePath(const QString &basePath);

    QVector<ChatSession> listSessions() const;
    bool saveSession(ChatSession &session, QString *errorMessage = nullptr) const;
    bool loadSession(const QString &sessionId, ChatSession *session, QString *errorMessage = nullptr) const;
    bool deleteSession(const QString &sessionId, QString *errorMessage = nullptr) const;

private:
    QString sessionFilePath(const QString &sessionId) const;
    bool ensureBasePath(QString *errorMessage = nullptr) const;
    ChatSession sessionFromJson(const QByteArray &data, QString *errorMessage) const;
    QByteArray sessionToJson(const ChatSession &session) const;

    QString m_basePath;
};

} // namespace llm_gui::core
