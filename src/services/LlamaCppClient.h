#pragma once

#include <functional>
#include <QAtomicInteger>
#include <QMutex>
#include <QThread>
#include <QStringList>

#include "services/ProviderClient.h"

namespace llm_gui::services {

class LlamaCppClient : public ProviderClient
{
    Q_OBJECT

public:
    explicit LlamaCppClient(QObject *parent = nullptr);
    ~LlamaCppClient() override;

    static bool isEmbeddedBackendAvailable();

    QString providerId() const override;
    QString displayName() const override;
    QString model() const override;
    void setModel(const QString &model) override;

    void setModelPath(const QString &path);
    QString modelPath() const;

    void setContextSize(int contextSize);
    int contextSize() const;

    void setGpuLayers(int gpuLayers);
    int gpuLayers() const;

    void setExtraArgs(const QString &args);
    QString extraArgs() const;

    void setRequestTimeoutMs(int timeoutMs);
    QStringList fetchAvailableModelsSync(QString *errorMessage = nullptr) const;

    void sendPrompt(const QString &prompt, bool stream) override;
    bool sendPromptSync(const QString &prompt, QString *response, QString *errorMessage) override;
    void cancel() override;

signals:
    void internalToken(const QString &token);
    void internalFinished(const QString &response);
    void internalFailed(const QString &message);

private:
    struct GenerationOptions {
        QString modelLabel;
        QString modelPath;
        int contextSize { 4096 };
        int gpuLayers { 0 };
        int timeoutMs { 300000 };
        QString extraArgs;
        bool stream { true };
    };

    static bool generateText(const QString &prompt,
                             const GenerationOptions &options,
                             const std::function<void (const QString &)> &onToken,
                             const std::function<bool ()> &isCancelled,
                             QString *response,
                             QString *errorMessage);

    GenerationOptions currentOptions() const;

    mutable QMutex m_mutex;
    QString m_modelLabel { QStringLiteral("local-gguf") };
    QString m_modelPath;
    int m_contextSize { 4096 };
    int m_gpuLayers { 0 };
    int m_requestTimeoutMs { 300000 };
    QString m_extraArgs;
    QThread *m_workerThread { nullptr };
    QAtomicInteger<int> m_cancelRequested { 0 };
};

} // namespace llm_gui::services
