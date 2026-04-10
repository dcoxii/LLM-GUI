#include "util/AttachmentProcessor.h"

#include <algorithm>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStringBuilder>

namespace llm_gui::util {
namespace {

constexpr qint64 kMaxDirectReadBytes = 512 * 1024;
constexpr int kMaxPromptCharsPerFile = 12000;
constexpr int kMaxZipEntries = 16;
constexpr qint64 kMaxZipEntryBytes = 256 * 1024;
constexpr int kMaxPromptCharsTotal = 30000;

QString fileTypeLabel(const QString &suffix)
{
    if (suffix == "docx") {
        return "DOCX document";
    }
    if (suffix == "zip") {
        return "ZIP archive";
    }
    if (suffix == "txt" || suffix == "md" || suffix == "rst") {
        return "Text document";
    }
    if (suffix == "json" || suffix == "yaml" || suffix == "yml" || suffix == "xml") {
        return "Structured text";
    }
    return "Source/text file";
}

} // namespace

QVector<ProcessedAttachment> AttachmentProcessor::processFiles(const QStringList &filePaths)
{
    QVector<ProcessedAttachment> attachments;
    attachments.reserve(filePaths.size());
    for (const QString &path : filePaths) {
        attachments.push_back(processSingleFile(path));
    }
    return attachments;
}

QString AttachmentProcessor::buildPromptEnvelope(const QString &userPrompt,
                                                 const QVector<ProcessedAttachment> &attachments)
{
    QString prompt = userPrompt.trimmed();
    QStringList blocks;
    int remainingBudget = kMaxPromptCharsTotal;

    for (const ProcessedAttachment &attachment : attachments) {
        if (!attachment.includedInPrompt || attachment.extractedText.trimmed().isEmpty()) {
            continue;
        }

        bool truncated = false;
        const QString boundedText = trimToBudget(attachment.extractedText, remainingBudget, &truncated);
        if (boundedText.trimmed().isEmpty()) {
            break;
        }

        QString block;
        block += QString("[Attached file: %1 | %2]\n")
                     .arg(attachment.displayName, attachment.typeLabel);
        block += boundedText;
        if (truncated) {
            block += "\n\n[Attachment content truncated due to size limits.]";
        }
        blocks << block;
        remainingBudget -= boundedText.size();
        if (remainingBudget <= 0) {
            break;
        }
    }

    if (blocks.isEmpty()) {
        return prompt;
    }

    return QString(
               "User request:\n%1\n\n"
               "You are also given local file content extracted by the desktop application. "
               "Use it as reference context for the user's request.\n\n"
               "%2")
        .arg(prompt, blocks.join("\n\n"));
}

QString AttachmentProcessor::buildAttachmentSummary(const QVector<ProcessedAttachment> &attachments)
{
    QStringList summary;
    for (const ProcessedAttachment &attachment : attachments) {
        QString line = QString("• %1 (%2)").arg(attachment.displayName, attachment.typeLabel);
        if (!attachment.warning.isEmpty()) {
            line += QString(" — %1").arg(attachment.warning);
        } else if (attachment.includedInPrompt) {
            line += " — included in prompt";
        } else {
            line += " — not included";
        }
        summary << line;
    }
    return summary.join('\n');
}

ProcessedAttachment AttachmentProcessor::processSingleFile(const QString &filePath)
{
    const QFileInfo info(filePath);
    ProcessedAttachment attachment;
    attachment.filePath = filePath;
    attachment.displayName = info.fileName();

    if (!info.exists() || !info.isFile()) {
        attachment.typeLabel = "Unavailable";
        attachment.warning = "File is missing or not a regular file.";
        return attachment;
    }

    const QString suffix = info.suffix().toLower();
    if (suffix == "docx") {
        return processDocxFile(filePath, info.fileName());
    }
    if (suffix == "zip") {
        return processZipFile(filePath, info.fileName());
    }
    if (isSupportedTextExtension(suffix)) {
        return processTextLikeFile(filePath, info.fileName(), fileTypeLabel(suffix));
    }

    attachment.typeLabel = QString("Unsupported (%1)").arg(suffix.isEmpty() ? "no extension" : suffix);
    attachment.warning = "Unsupported file type. Supported: text/code files, .docx, and .zip.";
    return attachment;
}

ProcessedAttachment AttachmentProcessor::processTextLikeFile(const QString &filePath,
                                                             const QString &displayName,
                                                             const QString &typeLabel)
{
    ProcessedAttachment attachment;
    attachment.filePath = filePath;
    attachment.displayName = displayName;
    attachment.typeLabel = typeLabel;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        attachment.warning = "Could not open file.";
        return attachment;
    }

    const QByteArray raw = file.read(kMaxDirectReadBytes + 1);
    if (raw.size() > kMaxDirectReadBytes) {
        attachment.warning = "File is large; only the beginning was included.";
    }

    attachment.extractedText = trimToBudget(QString::fromUtf8(raw), kMaxPromptCharsPerFile);
    attachment.includedInPrompt = !attachment.extractedText.trimmed().isEmpty();
    if (!attachment.includedInPrompt && attachment.warning.isEmpty()) {
        attachment.warning = "No readable text was extracted.";
    }
    return attachment;
}

ProcessedAttachment AttachmentProcessor::processDocxFile(const QString &filePath,
                                                         const QString &displayName)
{
    ProcessedAttachment attachment;
    attachment.filePath = filePath;
    attachment.displayName = displayName;
    attachment.typeLabel = "DOCX document";

    const QString xml = runCommandCapture("unzip", {"-p", filePath, "word/document.xml"});
    if (xml.isEmpty()) {
        attachment.warning = "Could not extract DOCX text. Ensure the unzip utility is installed.";
        return attachment;
    }

    attachment.extractedText = trimToBudget(stripXmlToText(xml), kMaxPromptCharsPerFile);
    attachment.includedInPrompt = !attachment.extractedText.trimmed().isEmpty();
    if (!attachment.includedInPrompt) {
        attachment.warning = "DOCX file did not yield readable text.";
    }
    return attachment;
}

ProcessedAttachment AttachmentProcessor::processZipFile(const QString &filePath,
                                                        const QString &displayName)
{
    ProcessedAttachment attachment;
    attachment.filePath = filePath;
    attachment.displayName = displayName;
    attachment.typeLabel = "ZIP archive";

    const QString listing = runCommandCapture("unzip", {"-Z1", filePath});
    if (listing.isEmpty()) {
        attachment.warning = "Could not inspect ZIP archive. Ensure the unzip utility is installed.";
        return attachment;
    }

    const QStringList entries = listing.split('\n', Qt::SkipEmptyParts);
    QStringList captured;
    int includedEntries = 0;

    for (const QString &entry : entries) {
        if (includedEntries >= kMaxZipEntries) {
            captured << "[Additional ZIP entries omitted due to limits.]";
            break;
        }

        const QString trimmedEntry = entry.trimmed();
        if (trimmedEntry.isEmpty() || trimmedEntry.endsWith('/')) {
            continue;
        }
        if (trimmedEntry.contains("../") || trimmedEntry.startsWith('/')) {
            continue;
        }

        const QFileInfo entryInfo(trimmedEntry);
        const QString suffix = entryInfo.suffix().toLower();
        if (!isSupportedTextExtension(suffix) && suffix != "docx") {
            continue;
        }

        QString entryText;
        if (suffix == "docx") {
            // For nested DOCX entries we currently skip; unzip path handling for nested archives is not supported here.
            continue;
        } else {
            entryText = runCommandCapture("unzip", {"-p", filePath, trimmedEntry});
        }

        if (entryText.isEmpty()) {
            continue;
        }

        entryText = trimToBudget(entryText, std::min<int>(kMaxPromptCharsPerFile / 2,
                                                          static_cast<int>(kMaxZipEntryBytes)));
        if (entryText.trimmed().isEmpty()) {
            continue;
        }

        captured << QString("[ZIP entry: %1]\n%2").arg(trimmedEntry, entryText);
        ++includedEntries;
    }

    attachment.extractedText = captured.join("\n\n");
    attachment.includedInPrompt = !attachment.extractedText.trimmed().isEmpty();
    if (!attachment.includedInPrompt) {
        attachment.warning = "No supported readable files were found inside the ZIP archive.";
    } else if (includedEntries < entries.size()) {
        attachment.warning = "Only supported text-like files from the ZIP were included.";
    }
    return attachment;
}

QString AttachmentProcessor::runCommandCapture(const QString &program,
                                               const QStringList &arguments,
                                               int timeoutMs)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(timeoutMs)) {
        return {};
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput());
}

QString AttachmentProcessor::stripXmlToText(const QString &xml)
{
    QString text = xml;
    text.replace(QRegularExpression(R"(<w:p[^>]*>)"), "\n\n");
    text.replace(QRegularExpression(R"(<w:tab[^>]*/>)"), "\t");
    text.replace(QRegularExpression(R"(<w:br[^>]*/>)"), "\n");
    text.replace(QRegularExpression(R"(<[^>]+>)"), " ");
    text.replace("&amp;", "&");
    text.replace("&lt;", "<");
    text.replace("&gt;", ">");
    text.replace("&quot;", "\"");
    return normalizeWhitespace(text);
}

QString AttachmentProcessor::normalizeWhitespace(QString text)
{
    text.replace(QRegularExpression(R"(\r\n?)"), "\n");
    text.replace(QRegularExpression(R"([\t ]+)"), " ");
    text.replace(QRegularExpression(R"(\n{3,})"), "\n\n");
    return text.trimmed();
}

QString AttachmentProcessor::trimToBudget(const QString &text, int maxChars, bool *truncated)
{
    if (truncated) {
        *truncated = false;
    }

    const QString normalized = normalizeWhitespace(text);
    if (normalized.size() <= maxChars) {
        return normalized;
    }

    if (truncated) {
        *truncated = true;
    }
    return normalized.left(std::max(0, maxChars));
}

bool AttachmentProcessor::isSupportedTextExtension(const QString &suffix)
{
    static const QSet<QString> supported{
        "txt", "md", "markdown", "rst", "json", "yaml", "yml", "xml", "csv", "log",
        "ini", "cfg", "conf", "toml",
        "c", "cc", "cpp", "cxx", "h", "hpp", "hh",
        "py", "js", "ts", "tsx", "jsx", "java", "kt", "rs", "go", "rb", "php",
        "swift", "cs", "sh", "bash", "zsh", "ps1", "sql", "html", "css"
    };
    return supported.contains(suffix);
}

} // namespace llm_gui::util
