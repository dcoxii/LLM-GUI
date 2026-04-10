#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

#include "tools/SandboxRunner.h"

class QJsonObject;

namespace llm_gui::tools {


struct ToolDefinition
{
    QString name;
    QString description;
    QString argumentSchema;
    bool isExternal { false };
    QString source;
};

struct ToolResult
{
    bool success { false };
    QString output;
};

struct PluginToolInfo
{
    QString name;
    QString description;
    QString argumentSchema;
    QStringList requiredScopes;
};

struct PluginInfo
{
    QString name;
    QString manifestPath;
    QString command;
    QString workingDirectory;
    int timeoutMs { 30000 };
    bool trusted { false };
    bool enabled { false };
    bool valid { false };
    QStringList errors;
    QVector<PluginToolInfo> tools;
    QStringList declaredScopes;
    QStringList grantedScopes;
    QStringList missingScopes;
    QString sandboxBackend;
    QString sandboxStatus;
    bool sandboxAvailable { false };
    int activeToolCount { 0 };
};

class ToolRegistry
{
public:
    ToolRegistry();

    QVector<ToolDefinition> definitions() const;
    QVector<PluginInfo> pluginInfos() const;
    QString usageInstructions() const;
    bool tryParseToolCall(const QString &assistantText, QString *toolName, QJsonObject *arguments, QString *errorMessage) const;
    ToolResult execute(const QString &toolName, const QJsonObject &arguments) const;
    QString reloadPlugins();
    QString pluginDirectory() const;
    void setPluginPolicy(const QStringList &trustedPlugins,
                         const QStringList &disabledPlugins,
                         const QMap<QString, QStringList> &grantedScopesByPlugin);

    static QStringList availablePluginScopes();
    static QString normalizePluginKey(const QString &pluginName);
    static QString scopeLabel(const QString &scope);

private:
    struct ExternalTool
    {
        QString pluginName;
        QString toolName;
        QString description;
        QString argumentSchema;
        QString command;
        QStringList args;
        QString workingDirectory;
        int timeoutMs { 30000 };
        QStringList requiredScopes;
    };

    QString normalizeAndExtractJson(const QString &assistantText) const;
    ToolResult runGetCurrentTime() const;
    ToolResult runListDirectory(const QJsonObject &arguments) const;
    ToolResult runReadTextFile(const QJsonObject &arguments) const;
    ToolResult runWriteTextFile(const QJsonObject &arguments) const;
    ToolResult runExternalTool(const ExternalTool &tool, const QJsonObject &arguments) const;

    QString validatePathArgument(const QJsonObject &arguments,
                                 const QString &key,
                                 bool requireExistingParent,
                                 QString *errorMessage) const;
    bool isPathAllowed(const QString &path) const;
    QString sanitizeDisplayPath(const QString &path) const;
    QStringList pluginSearchRoots() const;
    QStringList allowedRoots() const;

    QStringList m_allowedRoots;
    QVector<ExternalTool> m_externalTools;
    QVector<PluginInfo> m_pluginInfos;
    QStringList m_trustedPlugins;
    QStringList m_disabledPlugins;
    QMap<QString, QStringList> m_grantedScopesByPlugin;
    std::unique_ptr<SandboxRunner> m_sandboxRunner;
};

} // namespace llm_gui::tools
