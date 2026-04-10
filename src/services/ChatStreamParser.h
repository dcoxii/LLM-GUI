#pragma once

#include <QByteArray>
#include <QStringList>

namespace llm_gui::services {

class ChatStreamParser
{
public:
    QStringList push(const QByteArray &chunk);
    QString flushRemainder();

private:
    QByteArray m_buffer;
};

} // namespace llm_gui::services
