#include "util/TranscriptFormatter.h"

#include <QRegularExpression>
#include <QStringList>

namespace llm_gui::util {
namespace {

QString htmlEscape(QString value)
{
    value.replace('&', "&amp;");
    value.replace('<', "&lt;");
    value.replace('>', "&gt;");
    value.replace('"', "&quot;");
    return value;
}

QString inlineFormatting(const QString &text)
{
    QString escaped = htmlEscape(text);

    QRegularExpression inlineCodePattern(R"(`([^`]+)`)");    
    escaped.replace(inlineCodePattern, "<code style='background:#f3f4f6;padding:2px 4px;border-radius:4px;'>\\1</code>");

    QRegularExpression boldPattern(R"(\*\*(.+?)\*\*)");
    escaped.replace(boldPattern, "<b>\\1</b>");

    QRegularExpression italicPattern(R"((?<!\*)\*([^\*]+)\*(?!\*))");
    escaped.replace(italicPattern, "<i>\\1</i>");

    escaped.replace("\n", "<br/>");
    return escaped;
}

QString roleColor(const QString &role)
{
    if (role == "user") {
        return "#1f2937";
    }
    if (role == "assistant") {
        return "#111827";
    }
    if (role == "system") {
        return "#3f3f46";
    }
    if (role == "tool") {
        return "#312e81";
    }
    return "#111827";
}

}

QString TranscriptFormatter::formatMessages(const QVector<llm_gui::core::ChatMessage> &messages,
                                            const QString &activeAssistantBuffer)
{
    QString html;
    html += "<html><body style='font-family:sans-serif;background:#0b1220;color:#e5e7eb;margin:0;padding:12px;'>";
    for (const llm_gui::core::ChatMessage &message : messages) {
        html += formatMessage(message);
    }

    if (!activeAssistantBuffer.isEmpty()) {
        llm_gui::core::ChatMessage active;
        active.role = "assistant";
        active.content = activeAssistantBuffer;
        html += formatMessage(active);
    }

    html += "</body></html>";
    return html;
}

QString TranscriptFormatter::formatMessage(const llm_gui::core::ChatMessage &message)
{
    const QStringList lines = message.content.split('\n');
    QString rendered;
    bool inCodeBlock = false;
    QString codeLanguage;

    for (const QString &line : lines) {
        if (line.startsWith("```")) {
            if (!inCodeBlock) {
                inCodeBlock = true;
                codeLanguage = htmlEscape(line.mid(3).trimmed());
                rendered += "<div style='margin:8px 0;'>";
                if (!codeLanguage.isEmpty()) {
                    rendered += QString("<div style='font-size:11px;color:#555;margin-bottom:4px;'>%1</div>").arg(codeLanguage);
                }
                rendered += "<pre style='background:#111827;color:#e5e7eb;padding:12px;border-radius:8px;overflow:auto;'><code>";
            } else {
                inCodeBlock = false;
                rendered += "</code></pre></div>";
                codeLanguage.clear();
            }
            continue;
        }

        if (inCodeBlock) {
            rendered += htmlEscape(line) + "\n";
        } else if (line.startsWith("- ")) {
            rendered += QString("<div style='margin-left:12px;'>&bull; %1</div>").arg(inlineFormatting(line.mid(2)));
        } else {
            rendered += inlineFormatting(line) + "<br/>";
        }
    }

    if (inCodeBlock) {
        rendered += "</code></pre></div>";
    }

    return QString(
               "<div style='margin:12px 0;padding:12px;border:1px solid #374151;border-radius:10px;background:%3;color:#e5e7eb;'>"
               "<div style='font-weight:bold;margin-bottom:8px;color:#f9fafb;'>%1</div>"
               "<div style='color:#e5e7eb;'>%2</div>"
               "</div>")
        .arg(roleLabel(message.role), rendered, roleColor(message.role));
}

QString TranscriptFormatter::roleLabel(const QString &role)
{
    if (role == "user") {
        return "User";
    }
    if (role == "assistant") {
        return "Assistant";
    }
    if (role == "system") {
        return "System";
    }
    if (role == "tool") {
        return "Tool";
    }
    return role;
}

} // namespace llm_gui::util
