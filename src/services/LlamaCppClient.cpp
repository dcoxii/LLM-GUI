#include "services/LlamaCppClient.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegularExpression>
#include <QThread>

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#if __has_include(<llama.h>)
#include <llama.h>
#define LLM_GUI_HAS_LLAMA_CPP 1
#elif __has_include("llama.h")
#include "llama.h"
#define LLM_GUI_HAS_LLAMA_CPP 1
#else
#define LLM_GUI_HAS_LLAMA_CPP 0
#endif

namespace llm_gui::services {

namespace {
constexpr bool kEmbeddedBackendAvailable =
#if LLM_GUI_HAS_LLAMA_CPP
    true;
#else
    false;
#endif

struct ParsedExtraArgs {
    int predictTokens { 512 };
    int threads { 0 };
};

#if LLM_GUI_HAS_LLAMA_CPP
ParsedExtraArgs parseExtraArgs(const QString &value) {
    ParsedExtraArgs parsed;
    const QStringList parts = value.split(QRegularExpression(QStringLiteral(R"(\s+)")), Qt::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i) {
        const QString part = parts.at(i).trimmed();
        auto nextInt = [&](int fallback) {
            if (i + 1 >= parts.size()) {
                return fallback;
            }
            bool ok = false;
            const int v = parts.at(i + 1).toInt(&ok);
            if (ok) {
                ++i;
                return v;
            }
            return fallback;
        };

        if (part == QStringLiteral("-n") || part == QStringLiteral("--predict") || part == QStringLiteral("--n-predict")) {
            parsed.predictTokens = std::max(1, nextInt(parsed.predictTokens));
        } else if (part == QStringLiteral("-t") || part == QStringLiteral("--threads")) {
            parsed.threads = std::max(1, nextInt(parsed.threads));
        }
    }
    return parsed;
}

QString tokenPieceToQString(const llama_vocab * vocab, llama_token token) {
    std::vector<char> buffer(512);
    int n = llama_token_to_piece(vocab, token, buffer.data(), static_cast<int32_t>(buffer.size()), 0, true);
    if (n < 0) {
        buffer.resize(static_cast<size_t>(-n) + 16);
        n = llama_token_to_piece(vocab, token, buffer.data(), static_cast<int32_t>(buffer.size()), 0, true);
    }
    if (n <= 0) {
        return {};
    }
    return QString::fromUtf8(buffer.data(), n);
}

QString trimAssistantPrefix(QString value) {
    static const QStringList prefixes{
        QStringLiteral("assistant\n"),
        QStringLiteral("assistant:"),
        QStringLiteral("Assistant:"),
    };

    for (const QString &prefix : prefixes) {
        if (value.startsWith(prefix, Qt::CaseInsensitive)) {
            value.remove(0, prefix.size());
            break;
        }
    }
    return value;
}

struct BackendGuard {
    BackendGuard() { llama_backend_init(); }
    ~BackendGuard() { llama_backend_free(); }
};
#endif
} // namespace

LlamaCppClient::LlamaCppClient(QObject *parent)
    : ProviderClient(parent) {
    connect(this, &LlamaCppClient::internalToken, this, &LlamaCppClient::tokenReceived);
    connect(this, &LlamaCppClient::internalFinished, this, [this](const QString &) {
        emit statusChanged(QStringLiteral("Ready"));
        emit streamFinished();
    });
    connect(this, &LlamaCppClient::internalFailed, this, [this](const QString &message) {
        emit requestFailed(message);
        emit statusChanged(QStringLiteral("Ready"));
    });
}

LlamaCppClient::~LlamaCppClient() {
    cancel();
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
        delete m_workerThread;
        m_workerThread = nullptr;
    }
}

bool LlamaCppClient::isEmbeddedBackendAvailable() {
    return kEmbeddedBackendAvailable;
}

QString LlamaCppClient::providerId() const { return QStringLiteral("llama_cpp"); }
QString LlamaCppClient::displayName() const { return QStringLiteral("llama.cpp (integrated)"); }

QString LlamaCppClient::model() const {
    QMutexLocker locker(&m_mutex);
    return m_modelLabel;
}

void LlamaCppClient::setModel(const QString &model) {
    QMutexLocker locker(&m_mutex);
    m_modelLabel = model.trimmed().isEmpty() ? QStringLiteral("local-gguf") : model.trimmed();
}

void LlamaCppClient::setModelPath(const QString &path) {
    QMutexLocker locker(&m_mutex);
    m_modelPath = path.trimmed();
}

QString LlamaCppClient::modelPath() const {
    QMutexLocker locker(&m_mutex);
    return m_modelPath;
}

void LlamaCppClient::setContextSize(int contextSize) {
    QMutexLocker locker(&m_mutex);
    m_contextSize = std::max(256, contextSize);
}

int LlamaCppClient::contextSize() const {
    QMutexLocker locker(&m_mutex);
    return m_contextSize;
}

void LlamaCppClient::setGpuLayers(int gpuLayers) {
    QMutexLocker locker(&m_mutex);
    m_gpuLayers = gpuLayers;
}

int LlamaCppClient::gpuLayers() const {
    QMutexLocker locker(&m_mutex);
    return m_gpuLayers;
}

void LlamaCppClient::setExtraArgs(const QString &args) {
    QMutexLocker locker(&m_mutex);
    m_extraArgs = args.trimmed();
}

QString LlamaCppClient::extraArgs() const {
    QMutexLocker locker(&m_mutex);
    return m_extraArgs;
}

void LlamaCppClient::setRequestTimeoutMs(int timeoutMs) {
    QMutexLocker locker(&m_mutex);
    m_requestTimeoutMs = timeoutMs;
}

QStringList LlamaCppClient::fetchAvailableModelsSync(QString *errorMessage) const {
    if (errorMessage) {
        errorMessage->clear();
    }
    const QString configuredModel = model().trimmed().isEmpty() ? QStringLiteral("local-gguf") : model().trimmed();
    const QFileInfo modelInfo(modelPath());
    if (!modelInfo.exists()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Configured GGUF model file does not exist.");
        }
        return QStringList{configuredModel};
    }

    QStringList models{configuredModel, modelInfo.completeBaseName(), modelInfo.fileName()};
    models.removeDuplicates();
    return models;
}

LlamaCppClient::GenerationOptions LlamaCppClient::currentOptions() const {
    QMutexLocker locker(&m_mutex);
    GenerationOptions options;
    options.modelLabel = m_modelLabel;
    options.modelPath = m_modelPath;
    options.contextSize = m_contextSize;
    options.gpuLayers = m_gpuLayers;
    options.timeoutMs = m_requestTimeoutMs;
    options.extraArgs = m_extraArgs;
    return options;
}

void LlamaCppClient::sendPrompt(const QString &prompt, bool stream) {
    if (prompt.trimmed().isEmpty()) {
        emit requestFailed(QStringLiteral("Prompt is empty."));
        return;
    }

    if (m_workerThread) {
        cancel();
        m_workerThread->quit();
        m_workerThread->wait();
        delete m_workerThread;
        m_workerThread = nullptr;
    }

    m_cancelRequested.storeRelease(0);
    GenerationOptions options = currentOptions();
    options.stream = stream;

    emit statusChanged(QStringLiteral("Loading %1").arg(QFileInfo(options.modelPath).fileName()));
    emit streamStarted();

    m_workerThread = QThread::create([this, prompt, options]() {
        QString response;
        QString error;
        const bool ok = generateText(
            prompt,
            options,
            [this, options](const QString &token) {
                if (options.stream && !token.isEmpty()) {
                    emit internalToken(token);
                }
            },
            [this]() {
                return m_cancelRequested.loadAcquire() != 0;
            },
            &response,
            &error);

        if (ok) {
            if (!options.stream && !response.isEmpty()) {
                emit internalToken(response);
            }
            emit internalFinished(response);
        } else {
            emit internalFailed(error.isEmpty() ? QStringLiteral("Integrated llama.cpp generation failed.") : error);
        }
    });

    connect(m_workerThread, &QThread::finished, this, [this]() {
        if (m_workerThread) {
            m_workerThread->deleteLater();
            m_workerThread = nullptr;
        }
    });
    m_workerThread->start();
}

bool LlamaCppClient::sendPromptSync(const QString &prompt, QString *response, QString *errorMessage) {
    m_cancelRequested.storeRelease(0);
    GenerationOptions options = currentOptions();
    options.stream = false;
    return generateText(prompt,
                        options,
                        [](const QString &) {},
                        [this]() { return m_cancelRequested.loadAcquire() != 0; },
                        response,
                        errorMessage);
}

void LlamaCppClient::cancel() {
    m_cancelRequested.storeRelease(1);
    emit statusChanged(QStringLiteral("Cancelling..."));
}

bool LlamaCppClient::generateText(const QString &prompt,
                                  const GenerationOptions &options,
                                  const std::function<void (const QString &)> &onToken,
                                  const std::function<bool ()> &isCancelled,
                                  QString *response,
                                  QString *errorMessage) {
    if (response) {
        response->clear();
    }
    if (errorMessage) {
        errorMessage->clear();
    }

    if (prompt.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Prompt is empty.");
        }
        return false;
    }

#if !LLM_GUI_HAS_LLAMA_CPP
    Q_UNUSED(options);
    Q_UNUSED(onToken);
    Q_UNUSED(isCancelled);
    if (errorMessage) {
        *errorMessage = QStringLiteral("This build was configured without embedded llama.cpp sources.");
    }
    return false;
#else
    const QFileInfo modelInfo(options.modelPath);
    if (!modelInfo.exists() || !modelInfo.isFile()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Configured GGUF model file does not exist: %1").arg(options.modelPath);
        }
        return false;
    }

    static BackendGuard backendGuard;
    const ParsedExtraArgs parsed = parseExtraArgs(options.extraArgs);

    llama_model_params modelParams = llama_model_default_params();
    modelParams.n_gpu_layers = options.gpuLayers;

    std::unique_ptr<llama_model, decltype(&llama_model_free)> model(
        llama_model_load_from_file(options.modelPath.toUtf8().constData(), modelParams),
        &llama_model_free);
    if (!model) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to load GGUF model: %1").arg(options.modelPath);
        }
        return false;
    }

    const llama_vocab * vocab = llama_model_get_vocab(model.get());
    const QByteArray promptUtf8 = prompt.toUtf8();
    const int tokenCountRequired = -llama_tokenize(vocab,
                                                   promptUtf8.constData(),
                                                   static_cast<int32_t>(promptUtf8.size()),
                                                   nullptr,
                                                   0,
                                                   false,
                                                   true);
    if (tokenCountRequired <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Prompt tokenization failed.");
        }
        return false;
    }

    std::vector<llama_token> promptTokens(static_cast<size_t>(tokenCountRequired));
    if (llama_tokenize(vocab,
                       promptUtf8.constData(),
                       static_cast<int32_t>(promptUtf8.size()),
                       promptTokens.data(),
                       static_cast<int32_t>(promptTokens.size()),
                       false,
                       true) < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Prompt tokenization failed.");
        }
        return false;
    }

    const int predictTokens = std::max(1, std::min(parsed.predictTokens, std::max(1, options.contextSize - static_cast<int>(promptTokens.size()) - 1)));
    llama_context_params ctxParams = llama_context_default_params();
    ctxParams.n_ctx = static_cast<uint32_t>(std::max(options.contextSize, static_cast<int>(promptTokens.size()) + predictTokens + 1));
    ctxParams.n_batch = static_cast<uint32_t>(std::max(1, std::min<int>(promptTokens.size(), options.contextSize)));
    ctxParams.n_threads = parsed.threads > 0 ? parsed.threads : QThread::idealThreadCount();
    ctxParams.n_threads_batch = ctxParams.n_threads;
    ctxParams.no_perf = true;

    std::unique_ptr<llama_context, decltype(&llama_free)> ctx(
        llama_init_from_model(model.get(), ctxParams),
        &llama_free);
    if (!ctx) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to create llama.cpp context.");
        }
        return false;
    }

    auto samplerParams = llama_sampler_chain_default_params();
    samplerParams.no_perf = true;
    std::unique_ptr<llama_sampler, decltype(&llama_sampler_free)> sampler(
        llama_sampler_chain_init(samplerParams),
        &llama_sampler_free);
    if (!sampler) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to create llama.cpp sampler chain.");
        }
        return false;
    }

    llama_sampler_chain_add(sampler.get(), llama_sampler_init_greedy());

    llama_batch batch = llama_batch_get_one(promptTokens.data(), static_cast<int32_t>(promptTokens.size()));
    QString generated;
    generated.reserve(2048);

    QElapsedTimer timer;
    timer.start();
    for (int nPos = 0; nPos + batch.n_tokens < static_cast<int>(promptTokens.size()) + predictTokens; ) {
        if (isCancelled()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Generation cancelled.");
            }
            return false;
        }

        if (options.timeoutMs > 0 && timer.elapsed() > options.timeoutMs) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Integrated llama.cpp request timed out.");
            }
            return false;
        }

        const int decodeResult = llama_decode(ctx.get(), batch);
        if (decodeResult != 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("llama_decode failed with code %1.").arg(decodeResult);
            }
            return false;
        }

        nPos += batch.n_tokens;
        const llama_token next = llama_sampler_sample(sampler.get(), ctx.get(), -1);
        if (llama_vocab_is_eog(vocab, next)) {
            break;
        }

        const QString piece = tokenPieceToQString(vocab, next);
        generated += piece;
        onToken(piece);

        batch = llama_batch_get_one(const_cast<llama_token *>(&next), 1);
    }

    generated = trimAssistantPrefix(generated).trimmed();
    if (response) {
        *response = generated;
    }
    return true;
#endif
}

} // namespace llm_gui::services
