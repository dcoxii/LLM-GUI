#pragma once

#include <QMap>
#include <QSettings>
#include <QString>
#include <QStringList>

namespace llm_gui::core {

class AppSettings
{
public:
    AppSettings();

    QString provider() const;
    void setProvider(const QString &value);

    QString deepSeekBaseUrl() const;
    void setDeepSeekBaseUrl(const QString &value);

    QString deepSeekModel() const;
    void setDeepSeekModel(const QString &value);

    QString deepSeekApiKey() const;
    void setDeepSeekApiKey(const QString &value);

    QString huggingFaceToken() const;
    void setHuggingFaceToken(const QString &value);

    QString configuredLlamaCppModelPath() const;
    QString llamaCppModelPath() const;
    QStringList discoverLlamaCppModelPaths() const;
    QString defaultLlamaCppDownloadDirectory() const;
    void setLlamaCppModelPath(const QString &value);

    int llamaCppContextSize() const;
    void setLlamaCppContextSize(int value);

    int llamaCppGpuLayers() const;
    void setLlamaCppGpuLayers(int value);

    QString llamaCppExtraArgs() const;
    void setLlamaCppExtraArgs(const QString &value);

    QString llamaCppModelLabel() const;
    void setLlamaCppModelLabel(const QString &value);

    bool streamEnabled() const;
    void setStreamEnabled(bool enabled);

    QString sessionStoragePath() const;
    void setSessionStoragePath(const QString &value);

    int requestTimeoutMs() const;
    void setRequestTimeoutMs(int value);

    QString customInstructions() const;
    void setCustomInstructions(const QString &value);

    QStringList trustedPlugins() const;
    void setTrustedPlugins(const QStringList &values);

    QStringList disabledPlugins() const;
    void setDisabledPlugins(const QStringList &values);

    QStringList grantedPluginScopes(const QString &pluginName) const;
    void setGrantedPluginScopes(const QString &pluginName, const QStringList &values);
    QMap<QString, QStringList> allGrantedPluginScopes() const;

private:
    mutable QSettings m_settings;
};

} // namespace llm_gui::core
