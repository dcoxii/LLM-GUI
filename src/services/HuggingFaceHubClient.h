#pragma once

#include <QObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QVector>

class QWidget;

namespace llm_gui::services {

struct HuggingFaceFile
{
    QString name;
    qint64 sizeBytes { -1 };
};

struct HuggingFaceModel
{
    QString repoId;
    QString revision;
    qint64 downloads { 0 };
    qint64 likes { 0 };
    bool gated { false };
    bool privateRepo { false };
    QVector<HuggingFaceFile> ggufFiles;
};

class HuggingFaceHubClient : public QObject
{
    Q_OBJECT
public:
    explicit HuggingFaceHubClient(QObject *parent = nullptr);

    void setToken(const QString &token);
    void setRequestTimeoutMs(int timeoutMs);

    QVector<HuggingFaceModel> searchModelsSync(const QString &query, QString *errorMessage) const;
    QVector<HuggingFaceFile> fetchGgufFilesSync(const QString &repoId, QString *errorMessage) const;
    bool downloadFileSync(QWidget *parent,
                          const QString &repoId,
                          const QString &revision,
                          const QString &fileName,
                          const QString &destinationPath,
                          QString *errorMessage) const;

private:
    QString formatReplyError(QNetworkReply *reply, const QByteArray &body, const QString &fallback) const;
    void applyAuthHeader(QNetworkRequest &request) const;

    QString m_token;
    int m_requestTimeoutMs { 300000 };
};

} // namespace llm_gui::services
