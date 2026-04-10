#include "core/ChatSession.h"

#include <QUuid>

namespace llm_gui::core {

ChatSession ChatSession::createEmpty(const QString &provider, const QString &model)
{
    ChatSession session;
    session.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    session.title = "New Chat";
    session.provider = provider;
    session.model = model;
    session.createdAt = QDateTime::currentDateTimeUtc();
    session.updatedAt = session.createdAt;
    return session;
}

QString ChatSession::suggestedTitle() const
{
    for (const ChatMessage &message : messages) {
        if (message.role == "user" && !message.content.trimmed().isEmpty()) {
            QString line = message.content.trimmed();
            line.replace('\n', ' ');
            if (line.size() > 48) {
                line = line.left(48) + "...";
            }
            return line;
        }
    }
    return title.isEmpty() ? QStringLiteral("New Chat") : title;
}

} // namespace llm_gui::core
