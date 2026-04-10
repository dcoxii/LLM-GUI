#include "core/AppSettings.h"

#include <QCoreApplication>
#include <algorithm>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {
constexpr auto kDefaultProvider = "chatgpt";
constexpr auto kDefaultDeepSeekBaseUrl = "https://api.openai.com/v1";
constexpr auto kDefaultDeepSeekModel = "gpt-4.1-mini";
constexpr auto kDefaultLlamaCppContext = 4096;
constexpr auto kDefaultLlamaCppGpuLayers = 0;
constexpr auto kDefaultLlamaCppModelLabel = "local-gguf";
constexpr auto kDefaultTimeoutMs = 300000;
constexpr int kMaxDiscoveredLlamaModels = 64;

QString firstEnvironmentValue(const QStringList &names) {
    for (const QString &name : names) {
        const QByteArray value = qgetenv(name.toUtf8().constData());
        if (!value.trimmed().isEmpty()) {
            return QString::fromUtf8(value).trimmed();
        }
    }
    return {};
}

QString normalizeExistingFilePath(const QString &path) {
    const QFileInfo info(path.trimmed());
    if (!info.exists() || !info.isFile() || !info.isReadable()) {
        return {};
    }

    const QString canonicalPath = info.canonicalFilePath();
    return canonicalPath.isEmpty() ? info.absoluteFilePath() : canonicalPath;
}

QString normalizeExistingDirectoryPath(const QString &path) {
    const QFileInfo info(path.trimmed());
    if (!info.exists() || !info.isDir() || !info.isReadable()) {
        return {};
    }

    const QString canonicalPath = info.canonicalFilePath();
    return canonicalPath.isEmpty() ? info.absoluteFilePath() : canonicalPath;
}

QString autoLabelForModelPath(const QString &modelPath) {
    const QFileInfo info(modelPath.trimmed());
    const QString baseName = info.completeBaseName().trimmed();
    return baseName.isEmpty() ? info.fileName().trimmed() : baseName;
}

QStringList llamaModelSearchDirectories() {
    QStringList candidates{
        firstEnvironmentValue({QStringLiteral("LLM_GUI_MODEL_DIR")}),
        QDir::home().filePath(QStringLiteral(".config/LLM-GUI/model")),
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath(QStringLiteral("model")),
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("model")),
        QDir::home().filePath(QStringLiteral("Models")),
        QDir::home().filePath(QStringLiteral("models")),
        QDir::home().filePath(QStringLiteral("Documents/Models")),
        QDir::home().filePath(QStringLiteral("Downloads")),
        QDir::home().filePath(QStringLiteral("llama.cpp/models")),
        QDir::home().filePath(QStringLiteral(".local/share/llama.cpp/models"))
    };

    const QString envModelPath = firstEnvironmentValue({QStringLiteral("LLM_GUI_MODEL_PATH"), QStringLiteral("MODEL_PATH")});
    const QFileInfo envModelInfo(envModelPath);
    if (envModelInfo.exists()) {
        if (envModelInfo.isDir()) {
            candidates.prepend(envModelInfo.absoluteFilePath());
        } else {
            candidates.prepend(envModelInfo.absolutePath());
        }
    }

    QStringList normalized;
    for (const QString &candidate : candidates) {
        const QString trimmed = candidate.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        const QFileInfo info(trimmed);
        if (!info.exists() || !info.isReadable()) {
            continue;
        }

        const QString normalizedPath = info.canonicalFilePath().isEmpty()
            ? info.absoluteFilePath()
            : info.canonicalFilePath();
        if (!normalized.contains(normalizedPath)) {
            normalized << normalizedPath;
        }
    }
    return normalized;
}

QStringList discoverGgufModels(const QStringList &directories) {
    QList<QFileInfo> discoveredInfos;
    QStringList discoveredPaths;

    auto addModel = [&](const QFileInfo &info) {
        const QString normalizedPath = normalizeExistingFilePath(info.absoluteFilePath());
        if (normalizedPath.isEmpty() || discoveredPaths.contains(normalizedPath)) {
            return false;
        }

        discoveredPaths << normalizedPath;
        discoveredInfos << QFileInfo(normalizedPath);
        return true;
    };

    for (const QString &directory : directories) {
        QDirIterator it(directory,
                        {QStringLiteral("*.gguf"), QStringLiteral("*.GGUF")},
                        QDir::Files | QDir::Readable,
                        QDirIterator::Subdirectories);

        while (it.hasNext()) {
            const QString filePath = it.next();
            addModel(QFileInfo(filePath));
            if (discoveredPaths.size() >= kMaxDiscoveredLlamaModels) {
                break;
            }
        }

        if (discoveredPaths.size() >= kMaxDiscoveredLlamaModels) {
            break;
        }
    }

    std::sort(discoveredInfos.begin(), discoveredInfos.end(), [](const QFileInfo &lhs, const QFileInfo &rhs) {
        if (lhs.lastModified() != rhs.lastModified()) {
            return lhs.lastModified() > rhs.lastModified();
        }

        const int fileNameCompare = QString::compare(lhs.fileName(), rhs.fileName(), Qt::CaseInsensitive);
        if (fileNameCompare != 0) {
            return fileNameCompare < 0;
        }

        return QString::compare(lhs.absoluteFilePath(), rhs.absoluteFilePath(), Qt::CaseInsensitive) < 0;
    });

    QStringList result;
    for (const QFileInfo &info : discoveredInfos) {
        result << normalizeExistingFilePath(info.absoluteFilePath());
    }
    result.removeDuplicates();
    return result;
}

QString defaultLlamaDownloadDirectory() {
    const QString configuredModelPath = normalizeExistingFilePath(
        firstEnvironmentValue({QStringLiteral("LLM_GUI_MODEL_PATH"), QStringLiteral("MODEL_PATH")}));
    if (!configuredModelPath.isEmpty()) {
        const QString parentDir = normalizeExistingDirectoryPath(QFileInfo(configuredModelPath).absolutePath());
        if (!parentDir.isEmpty()) {
            return parentDir;
        }
    }

    const QString configuredDir = normalizeExistingDirectoryPath(firstEnvironmentValue({QStringLiteral("LLM_GUI_MODEL_DIR")}));
    if (!configuredDir.isEmpty()) {
        return configuredDir;
    }

    const QString appConfigModelDir = QDir::home().filePath(QStringLiteral(".config/LLM-GUI/model"));
    QDir().mkpath(appConfigModelDir);
    const QString normalizedAppConfigModelDir = normalizeExistingDirectoryPath(appConfigModelDir);
    if (!normalizedAppConfigModelDir.isEmpty()) {
        return normalizedAppConfigModelDir;
    }

    const QString downloadsDir = normalizeExistingDirectoryPath(QDir::home().filePath(QStringLiteral("Downloads")));
    if (!downloadsDir.isEmpty()) {
        return downloadsDir;
    }

    return QDir::homePath();
}

QString defaultLlamaModelPath() {
    const QString envPath = normalizeExistingFilePath(
        firstEnvironmentValue({QStringLiteral("LLM_GUI_MODEL_PATH"), QStringLiteral("MODEL_PATH")}));
    if (!envPath.isEmpty()) {
        return envPath;
    }

    const QStringList discovered = discoverGgufModels(llamaModelSearchDirectories());
    return discovered.isEmpty() ? QString() : discovered.front();
}

QString normalizePluginKey(const QString &pluginName) {
    QString key = pluginName.trimmed().toLower();
    key.replace(QRegularExpression(R"([^a-z0-9_\-]+)"), "_");
    key = key.trimmed();
    return key.isEmpty() ? QStringLiteral("plugin") : key;
}

QStringList normalizeList(QStringList values) {
    for (QString &value : values) {
        value = value.trimmed();
    }
    values.removeAll(QString());
    values.removeDuplicates();
    values.sort();
    return values;
}
} // namespace

namespace llm_gui::core {

AppSettings::AppSettings()
    : m_settings(QSettings::Format::IniFormat,
                 QSettings::UserScope,
                 QCoreApplication::organizationName(),
                 QCoreApplication::applicationName()) {}

QString AppSettings::provider() const {
    const QString configured = m_settings.value("providers/default", kDefaultProvider).toString().trimmed();
    if (configured.compare(QStringLiteral("deepseek"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("chatgpt");
    }
    return configured.isEmpty() ? QString::fromLatin1(kDefaultProvider) : configured;
}

void AppSettings::setProvider(const QString &value) {
    const QString trimmed = value.trimmed();
    m_settings.setValue("providers/default", trimmed.isEmpty() ? QString::fromLatin1(kDefaultProvider) : trimmed);
}

QString AppSettings::deepSeekBaseUrl() const {
    const QString chatGpt = m_settings.value("providers/chatgpt/base_url", QString()).toString().trimmed();
    if (!chatGpt.isEmpty()) {
        return chatGpt;
    }
    const QString legacy = m_settings.value("providers/deepseek/base_url", QString()).toString().trimmed();
    return legacy.isEmpty() ? QString::fromLatin1(kDefaultDeepSeekBaseUrl) : legacy;
}

void AppSettings::setDeepSeekBaseUrl(const QString &value) {
    const QString trimmed = value.trimmed();
    const QString normalized = trimmed.isEmpty() ? QString::fromLatin1(kDefaultDeepSeekBaseUrl) : trimmed;
    m_settings.setValue("providers/chatgpt/base_url", normalized);
    m_settings.setValue("providers/deepseek/base_url", normalized);
}

QString AppSettings::deepSeekModel() const {
    const QString chatGpt = m_settings.value("providers/chatgpt/model", QString()).toString().trimmed();
    if (!chatGpt.isEmpty()) {
        return chatGpt;
    }
    const QString legacy = m_settings.value("providers/deepseek/model", QString()).toString().trimmed();
    return legacy.isEmpty() ? QString::fromLatin1(kDefaultDeepSeekModel) : legacy;
}

void AppSettings::setDeepSeekModel(const QString &value) {
    const QString trimmed = value.trimmed();
    const QString normalized = trimmed.isEmpty() ? QString::fromLatin1(kDefaultDeepSeekModel) : trimmed;
    m_settings.setValue("providers/chatgpt/model", normalized);
    m_settings.setValue("providers/deepseek/model", normalized);
}

QString AppSettings::deepSeekApiKey() const {
    const QString chatGpt = m_settings.value("providers/chatgpt/api_key", QString()).toString().trimmed();
    if (!chatGpt.isEmpty()) {
        return chatGpt;
    }
    const QString legacy = m_settings.value("providers/deepseek/api_key", QString()).toString().trimmed();
    if (!legacy.isEmpty()) {
        return legacy;
    }
    return firstEnvironmentValue({QStringLiteral("OPENAI_API_KEY"), QStringLiteral("OPENAI_APIKEY")});
}

void AppSettings::setDeepSeekApiKey(const QString &value) {
    const QString trimmed = value.trimmed();
    m_settings.setValue("providers/chatgpt/api_key", trimmed);
    m_settings.setValue("providers/deepseek/api_key", trimmed);
}

QString AppSettings::huggingFaceToken() const {
    const QString configured = m_settings.value("providers/huggingface/token", QString()).toString().trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }
    return firstEnvironmentValue({
        QStringLiteral("HF_TOKEN"),
        QStringLiteral("HUGGING_FACE_HUB_TOKEN"),
        QStringLiteral("HUGGINGFACE_TOKEN")
    });
}

void AppSettings::setHuggingFaceToken(const QString &value) {
    m_settings.setValue("providers/huggingface/token", value.trimmed());
}

QString AppSettings::configuredLlamaCppModelPath() const {
    return m_settings.value("providers/llama_cpp/model_path", QString()).toString().trimmed();
}

QString AppSettings::llamaCppModelPath() const {
    const QString configured = configuredLlamaCppModelPath();
    const QString normalizedConfigured = normalizeExistingFilePath(configured);
    if (!normalizedConfigured.isEmpty()) {
        return normalizedConfigured;
    }

    const QString discoveredDefault = defaultLlamaModelPath();
    if (!discoveredDefault.isEmpty()) {
        return discoveredDefault;
    }

    return configured;
}

QStringList AppSettings::discoverLlamaCppModelPaths() const {
    QStringList discovered = discoverGgufModels(llamaModelSearchDirectories());

    const QString configured = normalizeExistingFilePath(configuredLlamaCppModelPath());
    if (!configured.isEmpty() && !discovered.contains(configured)) {
        discovered.prepend(configured);
    }

    const QString resolved = normalizeExistingFilePath(llamaCppModelPath());
    if (!resolved.isEmpty() && !discovered.contains(resolved)) {
        discovered.prepend(resolved);
    }

    discovered.removeDuplicates();
    return discovered;
}

QString AppSettings::defaultLlamaCppDownloadDirectory() const {
    const QString configured = normalizeExistingFilePath(configuredLlamaCppModelPath());
    if (!configured.isEmpty()) {
        const QString parentDir = normalizeExistingDirectoryPath(QFileInfo(configured).absolutePath());
        if (!parentDir.isEmpty()) {
            return parentDir;
        }
    }

    return defaultLlamaDownloadDirectory();
}

void AppSettings::setLlamaCppModelPath(const QString &value) {
    m_settings.setValue("providers/llama_cpp/model_path", value.trimmed());
}

int AppSettings::llamaCppContextSize() const {
    return m_settings.value("providers/llama_cpp/context_size", kDefaultLlamaCppContext).toInt();
}

void AppSettings::setLlamaCppContextSize(int value) {
    m_settings.setValue("providers/llama_cpp/context_size", value > 0 ? value : kDefaultLlamaCppContext);
}

int AppSettings::llamaCppGpuLayers() const {
    return m_settings.value("providers/llama_cpp/gpu_layers", kDefaultLlamaCppGpuLayers).toInt();
}

void AppSettings::setLlamaCppGpuLayers(int value) {
    m_settings.setValue("providers/llama_cpp/gpu_layers", value);
}

QString AppSettings::llamaCppExtraArgs() const {
    return m_settings.value("providers/llama_cpp/extra_args", QString()).toString();
}

void AppSettings::setLlamaCppExtraArgs(const QString &value) {
    m_settings.setValue("providers/llama_cpp/extra_args", value.trimmed());
}

bool AppSettings::streamEnabled() const {
    return m_settings.value("providers/stream_enabled", true).toBool();
}

void AppSettings::setStreamEnabled(bool enabled) {
    m_settings.setValue("providers/stream_enabled", enabled);
}

QString AppSettings::llamaCppModelLabel() const {
    const QString configured = m_settings.value("providers/llama_cpp/model_label", QString()).toString().trimmed();
    if (!configured.isEmpty() && configured != QString::fromLatin1(kDefaultLlamaCppModelLabel)) {
        return configured;
    }

    const QString resolvedPath = llamaCppModelPath();
    const QString autoLabel = autoLabelForModelPath(resolvedPath);
    if (!autoLabel.isEmpty()) {
        return autoLabel;
    }

    return configured.isEmpty() ? QString::fromLatin1(kDefaultLlamaCppModelLabel) : configured;
}

void AppSettings::setLlamaCppModelLabel(const QString &value) {
    const QString trimmed = value.trimmed();
    m_settings.setValue("providers/llama_cpp/model_label",
                        trimmed.isEmpty() ? QString::fromLatin1(kDefaultLlamaCppModelLabel) : trimmed);
}

QString AppSettings::sessionStoragePath() const {
    const QString defaultPath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                                    .filePath("sessions");
    return m_settings.value("storage/session_path", defaultPath).toString();
}

void AppSettings::setSessionStoragePath(const QString &value) {
    m_settings.setValue("storage/session_path", value.trimmed());
}

int AppSettings::requestTimeoutMs() const {
    return m_settings.value("network/request_timeout_ms", kDefaultTimeoutMs).toInt();
}

void AppSettings::setRequestTimeoutMs(int value) {
    m_settings.setValue("network/request_timeout_ms", value);
}

QString AppSettings::customInstructions() const {
    return m_settings.value("prompts/custom_instructions", QString()).toString();
}

void AppSettings::setCustomInstructions(const QString &value) {
    m_settings.setValue("prompts/custom_instructions", value);
}

QStringList AppSettings::trustedPlugins() const {
    return m_settings.value("plugins/trusted").toStringList();
}

void AppSettings::setTrustedPlugins(const QStringList &values) {
    m_settings.setValue("plugins/trusted", normalizeList(values));
}

QStringList AppSettings::disabledPlugins() const {
    return m_settings.value("plugins/disabled").toStringList();
}

void AppSettings::setDisabledPlugins(const QStringList &values) {
    m_settings.setValue("plugins/disabled", normalizeList(values));
}

QStringList AppSettings::grantedPluginScopes(const QString &pluginName) const {
    return normalizeList(m_settings.value(QString("plugins/scopes/%1").arg(normalizePluginKey(pluginName))).toStringList());
}

void AppSettings::setGrantedPluginScopes(const QString &pluginName, const QStringList &values) {
    m_settings.setValue(QString("plugins/scopes/%1").arg(normalizePluginKey(pluginName)), normalizeList(values));
}

QMap<QString, QStringList> AppSettings::allGrantedPluginScopes() const {
    QMap<QString, QStringList> result;
    m_settings.beginGroup("plugins/scopes");
    const QStringList keys = m_settings.childKeys();
    for (const QString &key : keys) {
        result.insert(key, normalizeList(m_settings.value(key).toStringList()));
    }
    m_settings.endGroup();
    return result;
}

} // namespace llm_gui::core
