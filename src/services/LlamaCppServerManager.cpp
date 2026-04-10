#include "services/LlamaCppServerManager.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>

namespace llm_gui::services {

namespace {
QString trimQuotes(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2) {
        const QChar first = value.front();
        const QChar last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            value = value.mid(1, value.size() - 2);
        }
    }
    return value.trimmed();
}

QStringList splitCommandLine(const QString &commandLine)
{
    QStringList parts;
    QString current;
    bool inSingle = false;
    bool inDouble = false;

    for (int i = 0; i < commandLine.size(); ++i) {
        const QChar ch = commandLine.at(i);
        if (ch == '\'' && !inDouble) {
            inSingle = !inSingle;
            continue;
        }
        if (ch == '"' && !inSingle) {
            inDouble = !inDouble;
            continue;
        }
        if (ch.isSpace() && !inSingle && !inDouble) {
            if (!current.isEmpty()) {
                parts << current;
                current.clear();
            }
            continue;
        }
        current += ch;
    }

    if (!current.isEmpty()) {
        parts << current;
    }

    for (QString &part : parts) {
        part = trimQuotes(part);
    }
    parts.removeAll(QString());
    return parts;
}
}

LlamaCppServerManager::LlamaCppServerManager(QObject *parent)
    : QObject(parent)
{
}

LlamaCppServerManager::~LlamaCppServerManager()
{
    stop();
}

void LlamaCppServerManager::setServerBinaryPath(const QString &value)
{
    m_serverBinaryPath = value.trimmed();
}

QString LlamaCppServerManager::serverBinaryPath() const
{
    return m_serverBinaryPath;
}

void LlamaCppServerManager::setModelPath(const QString &value)
{
    m_modelPath = value.trimmed();
}

QString LlamaCppServerManager::modelPath() const
{
    return m_modelPath;
}

void LlamaCppServerManager::setHost(const QString &value)
{
    m_host = value.trimmed().isEmpty() ? QStringLiteral("127.0.0.1") : value.trimmed();
}

QString LlamaCppServerManager::host() const
{
    return m_host;
}

void LlamaCppServerManager::setPort(int value)
{
    m_port = value > 0 ? value : 8080;
}

int LlamaCppServerManager::port() const
{
    return m_port;
}

void LlamaCppServerManager::setContextSize(int value)
{
    m_contextSize = value > 0 ? value : 4096;
}

int LlamaCppServerManager::contextSize() const
{
    return m_contextSize;
}

void LlamaCppServerManager::setGpuLayers(int value)
{
    m_gpuLayers = value;
}

int LlamaCppServerManager::gpuLayers() const
{
    return m_gpuLayers;
}

void LlamaCppServerManager::setExtraArguments(const QString &value)
{
    m_extraArguments = value.trimmed();
}

QString LlamaCppServerManager::extraArguments() const
{
    return m_extraArguments;
}

QString LlamaCppServerManager::baseUrl() const
{
    return QStringLiteral("http://%1:%2").arg(m_host, QString::number(m_port));
}

bool LlamaCppServerManager::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

bool LlamaCppServerManager::start(QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }

    if (isRunning()) {
        return true;
    }

    QStringList args = buildArguments(errorMessage);
    if (args.isEmpty() && errorMessage && !errorMessage->isEmpty()) {
        return false;
    }

    ensureProcess();
    m_process->setProgram(m_serverBinaryPath);
    m_process->setArguments(args);
    m_process->setWorkingDirectory(QFileInfo(m_serverBinaryPath).absolutePath());
    m_process->start();

    if (!m_process->waitForStarted(5000)) {
        const QString message = m_process->errorString().trimmed().isEmpty()
            ? QStringLiteral("Unable to start llama.cpp server process.")
            : m_process->errorString();
        if (errorMessage) {
            *errorMessage = message;
        }
        emit serverError(message);
        return false;
    }

    emit serverLogLine(QStringLiteral("==> llama.cpp server started: %1 %2")
                           .arg(m_serverBinaryPath, args.join(' ')));

    QString readinessError;
    if (!waitForServerReady(&readinessError)) {
        if (errorMessage) {
            *errorMessage = readinessError;
        }
        emit serverError(readinessError);
        stop();
        return false;
    }

    emit serverLogLine(QStringLiteral("==> llama.cpp server is ready at %1").arg(baseUrl()));
    emit serverStateChanged(true);
    return true;
}



bool LlamaCppServerManager::waitForServerReady(QString *errorMessage, int timeoutMs) const
{
    const QUrl url(baseUrl() + QStringLiteral("/v1/models"));
    QNetworkAccessManager manager;
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        if (!isRunning()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("llama.cpp server exited before becoming ready.");
            }
            return false;
        }

        QNetworkRequest request(url);
        request.setTransferTimeout(2000);

        QNetworkReply *reply = manager.get(request);
        QEventLoop loop;
        QTimer deadline;
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        deadline.start(2500);
        loop.exec();

        const bool finished = reply->isFinished();
        const auto replyError = reply->error();
        reply->deleteLater();

        if (finished && replyError == QNetworkReply::NoError) {
            return true;
        }

        QThread::msleep(250);
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("Timed out waiting for llama.cpp server readiness at %1.").arg(url.toString());
    }
    return false;
}

void LlamaCppServerManager::stop()
{
    if (!m_process) {
        return;
    }

    if (m_process->state() == QProcess::NotRunning) {
        return;
    }

    m_process->terminate();
    if (!m_process->waitForFinished(3000)) {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
}

QStringList LlamaCppServerManager::buildArguments(QString *errorMessage) const
{
    if (m_serverBinaryPath.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("llama.cpp server binary path is empty.");
        }
        return {};
    }

    const QFileInfo binaryInfo(m_serverBinaryPath);
    if (!binaryInfo.exists() || !binaryInfo.isFile()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("llama.cpp server binary was not found: %1").arg(m_serverBinaryPath);
        }
        return {};
    }

    if (m_modelPath.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("llama.cpp model path is empty.");
        }
        return {};
    }

    const QFileInfo modelInfo(m_modelPath);
    if (!modelInfo.exists()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("llama.cpp model was not found: %1").arg(m_modelPath);
        }
        return {};
    }

    QStringList args{
        QStringLiteral("--host"), m_host,
        QStringLiteral("--port"), QString::number(m_port),
        QStringLiteral("-m"), m_modelPath,
        QStringLiteral("-c"), QString::number(m_contextSize)
    };

    if (m_gpuLayers != 0) {
        args << QStringLiteral("--n-gpu-layers") << QString::number(m_gpuLayers);
    }

    const QStringList extra = splitCommandLine(m_extraArguments);
    for (const QString &part : extra) {
        if (!part.trimmed().isEmpty()) {
            args << part;
        }
    }

    return args;
}

void LlamaCppServerManager::ensureProcess()
{
    if (m_process) {
        return;
    }

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        emitOutputLines(m_process->readAllStandardOutput());
    });

    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        emitOutputLines(m_process->readAllStandardError());
    });

    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        emit serverError(m_process->errorString());
    });

    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString detail = (exitStatus == QProcess::NormalExit)
            ? QStringLiteral("llama.cpp server exited with code %1").arg(exitCode)
            : QStringLiteral("llama.cpp server crashed.");
        emit serverLogLine(QStringLiteral("==> %1").arg(detail));
        emit serverStateChanged(false);
    });
}

void LlamaCppServerManager::emitOutputLines(const QByteArray &buffer)
{
    const QString text = QString::fromUtf8(buffer);
    const QStringList lines = text.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        emit serverLogLine(line);
    }
}

} // namespace llm_gui::services
