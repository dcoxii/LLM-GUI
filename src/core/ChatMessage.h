#pragma once

#include <QString>
#include <QDateTime>

namespace llm_gui::core {

struct ChatMessage
{
    QString role;
    QString content;
    QDateTime timestamp;
};

} // namespace llm_gui::core
