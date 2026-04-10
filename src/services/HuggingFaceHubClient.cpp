#include "services/HuggingFaceHubClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <limits>

namespace llm_gui::services {

namespace {

constexpr auto kHuggingFaceApiBaseUrl = "https://huggingface.co/api";
constexpr auto kHuggingFaceWebBaseUrl = "https://huggingface.co";

QVector<HuggingFaceFile> parseGgufFiles(const QJsonArray &siblings)
{
    QVector<HuggingFaceFile> files;
    for (const QJsonValue &siblingValue : siblings) {
        const QJsonObject sibling = siblingValue.toObject();
        const QString fileName = sibling.value(QStringLiteral("rfilename")).toString().trimmed();
        if (!fileName.endsWith(QStringLiteral(".gguf"), Qt::CaseInsensitive)) {
            continue;
        }

        HuggingFaceFile file;
        file.name = fileName;
        file.sizeBytes = static_cast<qint64>(sibling.value(QStringLiteral("size")).toDouble(-1));
        files.append(file);
    }
    return files;
}

QUrl buildResolveUrl(const QString &repoId, const QString &revision, const QString &fileName)
{
    const QString resolvedRevision = revision.trimmed().isEmpty() ? QStringLiteral("main") : revision.trimmed();
    const QByteArray encodedPath =
        "/" + QUrl::toPercentEncoding(repoId, "/")
        + "/resolve/"
        + QUrl::toPercentEncoding(resolvedRevision)
        + "/"
        + QUrl::toPercentEncoding(fileName, "/");
    return QUrl::fromEncoded(QByteArray(kHuggingFaceWebBaseUrl) + encodedPath + QByteArray("?download=true"));
}

} // namespace

HuggingFaceHubClient::HuggingFaceHubClient(QObject *parent)
    : QObject(parent)
{
}

void HuggingFaceHubClient::setToken(const QString &token)
{
    m_token = token.trimmed();
}

void HuggingFaceHubClient::setRequestTimeoutMs(int timeoutMs)
{
    m_requestTimeoutMs = timeoutMs > 0 ? timeoutMs : 300000;
}

void HuggingFaceHubClient::applyAuthHeader(QNetworkRequest &request) const
{
    if (!m_token.isEmpty()) {
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_token).toUtf8());
    }
    request.setRawHeader("User-Agent",
                         QStringLiteral("%1/%2")
                             .arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion())
                             .toUtf8());
}

QString HuggingFaceHubClient::formatReplyError(QNetworkReply *reply,
                                               const QByteArray &body,
                                               const QString &fallback) const
{
    QStringList parts;

    const QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const QVariant reasonPhrase = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute);
    if (statusCode.isValid()) {
        QString status = QStringLiteral("HTTP %1").arg(statusCode.toInt());
        const QString reason = reasonPhrase.toString().trimmed();
        if (!reason.isEmpty()) {
            status += QStringLiteral(" %1").arg(reason);
        }
        parts << status;
    }

    QString bodyText = QString::fromUtf8(body).simplified();
    if (bodyText.isEmpty()) {
        bodyText = reply->errorString().trimmed();
    }
    if (!bodyText.isEmpty()) {
        if (bodyText.size() > 400) {
            bodyText.truncate(400);
            bodyText += QStringLiteral("...");
        }
        parts << bodyText;
    }

    return parts.isEmpty() ? fallback : parts.join(QStringLiteral(" - "));
}

QVector<HuggingFaceModel> HuggingFaceHubClient::searchModelsSync(const QString &query, QString *errorMessage) const
{
    if (errorMessage) {
        errorMessage->clear();
    }

    QUrl url(QStringLiteral("%1/models").arg(QString::fromLatin1(kHuggingFaceApiBaseUrl)));
    QUrlQuery urlQuery;
    urlQuery.addQueryItem(QStringLiteral("search"), query.trimmed());
    urlQuery.addQueryItem(QStringLiteral("limit"), QStringLiteral("25"));
    urlQuery.addQueryItem(QStringLiteral("sort"), QStringLiteral("downloads"));
    urlQuery.addQueryItem(QStringLiteral("direction"), QStringLiteral("-1"));
    urlQuery.addQueryItem(QStringLiteral("full"), QStringLiteral("true"));
    url.setQuery(urlQuery);

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    applyAuthHeader(request);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QNetworkReply *reply = manager.get(request);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(m_requestTimeoutMs);
    loop.exec();

    if (timer.isActive()) {
        timer.stop();
    } else {
        reply->abort();
        if (errorMessage) {
            *errorMessage = QStringLiteral("Timed out while searching Hugging Face.");
        }
        reply->deleteLater();
        return {};
    }

    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        if (errorMessage) {
            *errorMessage = formatReplyError(reply, body, QStringLiteral("Unable to search Hugging Face models."));
        }
        reply->deleteLater();
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to parse Hugging Face search results.");
        }
        reply->deleteLater();
        return {};
    }

    QVector<HuggingFaceModel> results;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        HuggingFaceModel model;
        model.repoId = object.value(QStringLiteral("id")).toString().trimmed();
        model.revision = object.value(QStringLiteral("sha")).toString().trimmed();
        model.downloads = static_cast<qint64>(object.value(QStringLiteral("downloads")).toDouble(0));
        model.likes = static_cast<qint64>(object.value(QStringLiteral("likes")).toDouble(0));
        model.gated = object.value(QStringLiteral("gated")).toBool(false);
        model.privateRepo = object.value(QStringLiteral("private")).toBool(false);
        model.ggufFiles = parseGgufFiles(object.value(QStringLiteral("siblings")).toArray());

        if (!model.repoId.isEmpty() && !model.ggufFiles.isEmpty()) {
            results.append(model);
        }
    }

    reply->deleteLater();
    return results;
}

QVector<HuggingFaceFile> HuggingFaceHubClient::fetchGgufFilesSync(const QString &repoId, QString *errorMessage) const
{
    if (errorMessage) {
        errorMessage->clear();
    }

    const QString trimmedRepoId = repoId.trimmed();
    if (trimmedRepoId.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Missing Hugging Face repository id.");
        }
        return {};
    }

    QUrl url(QStringLiteral("%1/models/%2")
                 .arg(QString::fromLatin1(kHuggingFaceApiBaseUrl),
                      QString::fromUtf8(QUrl::toPercentEncoding(trimmedRepoId, "/"))));

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    applyAuthHeader(request);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QNetworkReply *reply = manager.get(request);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(m_requestTimeoutMs);
    loop.exec();

    if (timer.isActive()) {
        timer.stop();
    } else {
        reply->abort();
        if (errorMessage) {
            *errorMessage = QStringLiteral("Timed out while loading repository details.");
        }
        reply->deleteLater();
        return {};
    }

    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        if (errorMessage) {
            *errorMessage = formatReplyError(reply, body, QStringLiteral("Unable to load repository details."));
        }
        reply->deleteLater();
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to parse Hugging Face repository details.");
        }
        reply->deleteLater();
        return {};
    }

    reply->deleteLater();
    return parseGgufFiles(document.object().value(QStringLiteral("siblings")).toArray());
}

bool HuggingFaceHubClient::downloadFileSync(QWidget *parent,
                                            const QString &repoId,
                                            const QString &revision,
                                            const QString &fileName,
                                            const QString &destinationPath,
                                            QString *errorMessage) const
{
    if (errorMessage) {
        errorMessage->clear();
    }

    const QFileInfo destinationInfo(destinationPath);
    QDir destinationDir(destinationInfo.absolutePath());
    if (!destinationDir.exists() && !destinationDir.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to create destination directory: %1").arg(destinationInfo.absolutePath());
        }
        return false;
    }

    const QString partialPath = destinationPath + QStringLiteral(".part");
    QFile outputFile(partialPath);
    if (!outputFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to write download file: %1").arg(partialPath);
        }
        return false;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(buildResolveUrl(repoId, revision, fileName));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    applyAuthHeader(request);

    QProgressDialog progress(parent);
    progress.setWindowTitle(QStringLiteral("Downloading GGUF"));
    progress.setLabelText(QStringLiteral("Downloading %1").arg(QFileInfo(fileName).fileName()));
    progress.setCancelButtonText(QStringLiteral("Cancel"));
    progress.setMinimumDuration(0);
    progress.setRange(0, 0);
    progress.setValue(0);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QNetworkReply *reply = manager.get(request);
    QObject::connect(reply, &QNetworkReply::readyRead, &outputFile, [&outputFile, reply]() {
        outputFile.write(reply->readAll());
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, &progress, [&progress](qint64 received, qint64 total) {
        if (total > 0) {
            progress.setRange(0, static_cast<int>(qMin<qint64>(total, std::numeric_limits<int>::max())));
            progress.setValue(static_cast<int>(qMin<qint64>(received, std::numeric_limits<int>::max())));
        }
    });
    QObject::connect(&progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start(m_requestTimeoutMs);
    progress.show();
    loop.exec();

    if (reply->bytesAvailable() > 0) {
        outputFile.write(reply->readAll());
    }
    outputFile.flush();
    outputFile.close();

    if (timer.isActive()) {
        timer.stop();
    } else {
        reply->abort();
        QFile::remove(partialPath);
        if (errorMessage) {
            *errorMessage = QStringLiteral("Timed out while downloading the selected GGUF file.");
        }
        reply->deleteLater();
        return false;
    }

    if (reply->error() != QNetworkReply::NoError) {
        const QByteArray body = reply->readAll();
        QFile::remove(partialPath);
        if (errorMessage) {
            *errorMessage = formatReplyError(reply, body, QStringLiteral("Unable to download the selected GGUF file."));
        }
        reply->deleteLater();
        return false;
    }

    QFile::remove(destinationPath);
    if (!QFile::rename(partialPath, destinationPath)) {
        QFile::remove(partialPath);
        if (errorMessage) {
            *errorMessage = QStringLiteral("Download finished, but the file could not be moved into place.");
        }
        reply->deleteLater();
        return false;
    }

    reply->deleteLater();
    return true;
}

} // namespace llm_gui::services
