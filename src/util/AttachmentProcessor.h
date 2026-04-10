#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace llm_gui::util {

struct ProcessedAttachment
{
    QString filePath;
    QString displayName;
    QString typeLabel;
    QString extractedText;
    QString warning;
    bool includedInPrompt { false };
};

class AttachmentProcessor
{
public:
    static QVector<ProcessedAttachment> processFiles(const QStringList &filePaths);
    static QString buildPromptEnvelope(const QString &userPrompt,
                                       const QVector<ProcessedAttachment> &attachments);
    static QString buildAttachmentSummary(const QVector<ProcessedAttachment> &attachments);

private:
    static ProcessedAttachment processSingleFile(const QString &filePath);
    static ProcessedAttachment processTextLikeFile(const QString &filePath,
                                                   const QString &displayName,
                                                   const QString &typeLabel);
    static ProcessedAttachment processDocxFile(const QString &filePath,
                                               const QString &displayName);
    static ProcessedAttachment processZipFile(const QString &filePath,
                                              const QString &displayName);
    static QString runCommandCapture(const QString &program, const QStringList &arguments, int timeoutMs = 10000);
    static QString stripXmlToText(const QString &xml);
    static QString normalizeWhitespace(QString text);
    static QString trimToBudget(const QString &text, int maxChars, bool *truncated = nullptr);
    static bool isSupportedTextExtension(const QString &suffix);
};

} // namespace llm_gui::util
