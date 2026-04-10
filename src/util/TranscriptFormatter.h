#pragma once

#include <QString>
#include <QVector>

#include "core/ChatMessage.h"

namespace llm_gui::util {

class TranscriptFormatter
{
public:
    static QString formatMessages(const QVector<llm_gui::core::ChatMessage> &messages,
                                  const QString &activeAssistantBuffer = QString());

private:
    static QString formatMessage(const llm_gui::core::ChatMessage &message);
    static QString roleLabel(const QString &role);

};

} // namespace llm_gui::util
