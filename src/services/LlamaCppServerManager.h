#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

namespace llm_gui::services {

class LlamaCppServerManager : public QObject
{
    Q_OBJECT

public:
    explicit LlamaCppServerManager(QObject *parent = nullptr);
    ~LlamaCppServerManager() override;

    void setServerBinaryPath(const QString &value);
    QString serverBinaryPath() const;

    void setModelPath(const QString &value);
    QString modelPath() const;

    void setHost(const QString &value);
    QString host() const;

    void setPort(int value);
    int port() const;

    void setContextSize(int value);
    int contextSize() const;

    void setGpuLayers(int value);
    int gpuLayers() const;

    void setExtraArguments(const QString &value);
    QString extraArguments() const;

    QString baseUrl() const;
    bool isRunning() const;

public slots:
    bool start(QString *errorMessage = nullptr);
    void stop();

signals:
    void serverStateChanged(bool running);
    void serverLogLine(const QString &line);
    void serverError(const QString &message);

private:
    QStringList buildArguments(QString *errorMessage) const;
    bool waitForServerReady(QString *errorMessage, int timeoutMs = 15000) const;
    void ensureProcess();
    void emitOutputLines(const QByteArray &buffer);

    QProcess *m_process { nullptr };
    QString m_serverBinaryPath;
    QString m_modelPath;
    QString m_host { QStringLiteral("127.0.0.1") };
    int m_port { 8080 };
    int m_contextSize { 4096 };
    int m_gpuLayers { 0 };
    QString m_extraArguments;
};

} // namespace llm_gui::services
