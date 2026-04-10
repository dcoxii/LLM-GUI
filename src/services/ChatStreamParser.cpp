#include "services/ChatStreamParser.h"

namespace llm_gui::services {

QStringList ChatStreamParser::push(const QByteArray &chunk)
{
    m_buffer.append(chunk);
    QStringList lines;

    int newlineIndex = m_buffer.indexOf('\n');
    while (newlineIndex >= 0) {
        const QByteArray line = m_buffer.left(newlineIndex).trimmed();
        m_buffer.remove(0, newlineIndex + 1);

        if (!line.isEmpty()) {
            lines.append(QString::fromUtf8(line));
        }

        newlineIndex = m_buffer.indexOf('\n');
    }

    return lines;
}

QString ChatStreamParser::flushRemainder()
{
    const QByteArray line = m_buffer.trimmed();
    m_buffer.clear();
    return QString::fromUtf8(line);
}

} // namespace llm_gui::services
