#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

#include "core/ChatMessage.h"

namespace llm_gui::core {

class ChatSession
{
public:
    QString id;
    QString title;
    QString provider;
    QString model;
    QDateTime createdAt;
    QDateTime updatedAt;
    QVector<ChatMessage> messages;

    static ChatSession createEmpty(const QString &provider, const QString &model);
    QString suggestedTitle() const;
};

} // namespace llm_gui::core
