#include "tools/ToolRegistry.h"
#include "tools/SandboxRunner.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace llm_gui::tools {

namespace {
constexpr qint64 kMaxReadBytes = 256 * 1024;
constexpr qint64 kMaxWriteBytes = 256 * 1024;
constexpr int kMaxProcessOutputBytes = 512 * 1024;

QString prettyJson(const QJsonValue &value)
{
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    if (value.isArray()) {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }
    if (value.isString()) {
        return value.toString();
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"value", value}}).toJson(QJsonDocument::Compact));
}

QStringList normalizeScopes(QStringList scopes)
{
    for (QString &scope : scopes) {
        scope = scope.trimmed().toLower();
    }
    scopes.removeAll(QString());
    scopes.removeDuplicates();
    scopes.sort();
    return scopes;
}

QStringList toScopeList(const QJsonValue &value)
{
    QStringList scopes;
    if (value.isArray()) {
        for (const QJsonValue &entry : value.toArray()) {
            scopes << entry.toString();
        }
    } else if (value.isString()) {
        scopes = value.toString().split(',', Qt::SkipEmptyParts);
    }
    return normalizeScopes(scopes);
}

QString manifestPluginKey(const QFileInfo &manifestInfo, const QJsonObject *rootObject = nullptr)
{
    QString pluginName = manifestInfo.baseName();
    if (rootObject) {
        const QString manifestName = rootObject->value("name").toString().trimmed();
        if (!manifestName.isEmpty()) {
            pluginName = manifestName;
        }
    }
    return ToolRegistry::normalizePluginKey(pluginName);
}

QString userPluginDirectory()
{
    return QDir::cleanPath(QDir::home().filePath(QStringLiteral(".config/LLM-GUI/plugins")));
}

QStringList bundledPluginRoots()
{
    QStringList roots;

    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        const QDir exeDir(appDir);
        roots << QDir::cleanPath(exeDir.absoluteFilePath(QStringLiteral("../share/llm-gui/plugins")));
        roots << QDir::cleanPath(exeDir.absoluteFilePath(QStringLiteral("../plugins")));
        roots << QDir::cleanPath(exeDir.absoluteFilePath(QStringLiteral("plugins")));
    }

    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty()) {
        roots << QDir::cleanPath(QDir(appData).filePath(QStringLiteral("plugins")));
    }

    const QStringList genericDataRoots =
        QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString &dataRoot : genericDataRoots) {
        if (!dataRoot.isEmpty()) {
            roots << QDir::cleanPath(QDir(dataRoot).filePath(QStringLiteral("llm-gui/plugins")));
        }
    }

    roots.removeAll(QString());
    roots.removeDuplicates();
    return roots;
}

void seedUserPluginDirectory(const QString &targetRoot)
{
    if (targetRoot.isEmpty()) {
        return;
    }

    QDir targetDir(targetRoot);
    targetDir.mkpath(QStringLiteral("."));

    const QFileInfoList existingManifests =
        targetDir.entryInfoList({QStringLiteral("*.json")}, QDir::Files | QDir::Readable, QDir::Name);
    if (!existingManifests.isEmpty()) {
        return;
    }

    for (const QString &sourceRoot : bundledPluginRoots()) {
        const QDir sourceDir(sourceRoot);
        if (!sourceDir.exists() || QDir::cleanPath(sourceRoot) == QDir::cleanPath(targetRoot)) {
            continue;
        }

        const QFileInfoList sourceFiles = sourceDir.entryInfoList(
            {QStringLiteral("*.json"), QStringLiteral("*.py"), QStringLiteral("README.txt")},
            QDir::Files | QDir::Readable,
            QDir::Name);

        bool copiedAny = false;
        for (const QFileInfo &sourceFile : sourceFiles) {
            const QString targetPath = targetDir.filePath(sourceFile.fileName());
            if (QFile::exists(targetPath)) {
                continue;
            }
            if (QFile::copy(sourceFile.absoluteFilePath(), targetPath)) {
                copiedAny = true;
            }
        }

        if (copiedAny) {
            break;
        }
    }
}
}

QStringList ToolRegistry::availablePluginScopes()
{
    return QStringList{
        QStringLiteral("filesystem.read"),
        QStringLiteral("filesystem.write"),
        QStringLiteral("network"),
        QStringLiteral("process")
    };
}

QString ToolRegistry::scopeLabel(const QString &scope)
{
    if (scope == "filesystem.read") return "Read local files";
    if (scope == "filesystem.write") return "Write local files";
    if (scope == "network") return "Network access";
    if (scope == "process") return "Launch subprocesses";
    return scope;
}

QString ToolRegistry::normalizePluginKey(const QString &pluginName)
{
    QString key = pluginName.trimmed().toLower();
    key.replace(QRegularExpression(R"([^a-z0-9_\-]+)"), "_");
    key = key.trimmed();
    return key.isEmpty() ? QStringLiteral("plugin") : key;
}

ToolRegistry::ToolRegistry()
{
    const QString home = QDir::cleanPath(QDir::homePath());
    const QString docs = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    const QString downloads = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));

    if (!home.isEmpty()) {
        m_allowedRoots << home;
    }
    if (!docs.isEmpty() && !m_allowedRoots.contains(docs)) {
        m_allowedRoots << docs;
    }
    if (!downloads.isEmpty() && !m_allowedRoots.contains(downloads)) {
        m_allowedRoots << downloads;
    }

    m_sandboxRunner = std::make_unique<BubblewrapRunner>();
    seedUserPluginDirectory(pluginDirectory());
    reloadPlugins();
}

void ToolRegistry::setPluginPolicy(const QStringList &trustedPlugins,
                                   const QStringList &disabledPlugins,
                                   const QMap<QString, QStringList> &grantedScopesByPlugin)
{
    m_trustedPlugins = trustedPlugins;
    m_trustedPlugins.removeDuplicates();
    m_trustedPlugins.sort();

    m_disabledPlugins = disabledPlugins;
    m_disabledPlugins.removeDuplicates();
    m_disabledPlugins.sort();

    m_grantedScopesByPlugin.clear();
    for (auto it = grantedScopesByPlugin.constBegin(); it != grantedScopesByPlugin.constEnd(); ++it) {
        m_grantedScopesByPlugin.insert(it.key(), normalizeScopes(it.value()));
    }
}

QVector<ToolDefinition> ToolRegistry::definitions() const
{
    QVector<ToolDefinition> defs = {
        {"get_current_time", "Get the local current time in ISO format.", R"({})", false, "built-in"},
        {"list_directory", "List files and folders inside a directory.", R"({"type":"object","properties":{"path":{"type":"string","description":"Absolute directory path"}},"required":["path"]})", false, "built-in"},
        {"read_text_file", "Read a UTF-8 text file up to 256 KB.", R"({"type":"object","properties":{"path":{"type":"string","description":"Absolute file path"}},"required":["path"]})", false, "built-in"},
        {"write_text_file", "Write UTF-8 text to a file up to 256 KB.", R"({"type":"object","properties":{"path":{"type":"string"},"content":{"type":"string"}},"required":["path","content"]})", false, "built-in"}
    };

    for (const ExternalTool &tool : m_externalTools) {
        defs.push_back({tool.toolName, tool.description, tool.argumentSchema, true, tool.pluginName});
    }
    return defs;
}

QVector<PluginInfo> ToolRegistry::pluginInfos() const
{
    return m_pluginInfos;
}

QString ToolRegistry::usageInstructions() const
{
    QStringList lines;
    lines << "You can use local tools and loaded plugins when needed.";
    lines << "If a tool is needed, reply with ONLY one JSON object and no other text.";
    lines << "Schema:";
    lines << R"({"tool_call":{"name":"tool_name","arguments":{...}}})";
    lines << "Available tools:";

    for (const ToolDefinition &def : definitions()) {
        const QString source = def.isExternal ? QString("plugin:%1").arg(def.source) : def.source;
        lines << QString("- %1 [%2]: %3").arg(def.name, source, def.description);
        lines << QString("  arguments: %1").arg(def.argumentSchema);
    }

    if (m_sandboxRunner) {
        lines << QString("Plugin sandbox: %1 (%2)").arg(m_sandboxRunner->backendName(), m_sandboxRunner->availabilityMessage());
    }
    lines << QString("Plugin directory: %1").arg(pluginDirectory());
    lines << "Only trusted and enabled plugins with granted scopes are exposed to the model.";
    lines << QString("Recognized plugin scopes: %1").arg(availablePluginScopes().join(", "));
    lines << "Allowed filesystem scope for built-in file tools: the user's home, Documents, and Downloads folders.";
    lines << "When you do not need a tool, answer normally in plain text.";
    return lines.join('\n');
}

QString ToolRegistry::normalizeAndExtractJson(const QString &assistantText) const
{
    QString text = assistantText.trimmed();
    if (text.startsWith("```") && text.endsWith("```")) {
        int firstNewline = text.indexOf('\n');
        if (firstNewline >= 0) {
            text = text.mid(firstNewline + 1);
        }
        if (text.endsWith("```")) {
            text.chop(3);
        }
        text = text.trimmed();
    }
    return text;
}

bool ToolRegistry::tryParseToolCall(const QString &assistantText, QString *toolName, QJsonObject *arguments, QString *errorMessage) const
{
    const QString text = normalizeAndExtractJson(assistantText);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Assistant response was not valid tool-call JSON.");
        }
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonObject toolCall = root.value("tool_call").toObject();
    if (toolCall.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("JSON did not include a tool_call object.");
        }
        return false;
    }

    if (toolName) {
        *toolName = toolCall.value("name").toString().trimmed();
    }
    if (arguments) {
        *arguments = toolCall.value("arguments").toObject();
    }
    if (toolName && toolName->isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("tool_call.name was empty.");
        }
        return false;
    }
    return true;
}

ToolResult ToolRegistry::execute(const QString &toolName, const QJsonObject &arguments) const
{
    if (toolName == "get_current_time") {
        return runGetCurrentTime();
    }
    if (toolName == "list_directory") {
        return runListDirectory(arguments);
    }
    if (toolName == "read_text_file") {
        return runReadTextFile(arguments);
    }
    if (toolName == "write_text_file") {
        return runWriteTextFile(arguments);
    }

    for (const ExternalTool &tool : m_externalTools) {
        if (tool.toolName == toolName) {
            return runExternalTool(tool, arguments);
        }
    }

    ToolResult result;
    result.output = QStringLiteral("Unknown or unavailable tool: %1").arg(toolName);
    return result;
}

QStringList ToolRegistry::allowedRoots() const
{
    return m_allowedRoots;
}

QStringList ToolRegistry::pluginSearchRoots() const
{
    QStringList roots;

    const QString userRoot = userPluginDirectory();
    if (!userRoot.isEmpty()) {
        roots << userRoot;
    }

    const QString genericConfig = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (!genericConfig.isEmpty()) {
        const QString configRoot = QDir::cleanPath(QDir(genericConfig).filePath(QStringLiteral("LLM-GUI/plugins")));
        if (!roots.contains(configRoot)) {
            roots << configRoot;
        }
    }

    for (const QString &root : bundledPluginRoots()) {
        if (!roots.contains(root)) {
            roots << root;
        }
    }

    roots.removeAll(QString());
    roots.removeDuplicates();
    return roots;
}

QString ToolRegistry::pluginDirectory() const
{
    const QStringList roots = pluginSearchRoots();
    return roots.isEmpty() ? QString() : roots.first();
}

QString ToolRegistry::reloadPlugins()
{
    m_externalTools.clear();
    m_pluginInfos.clear();

    QStringList loaded;
    QStringList errors;
    QStringList shadowed;
    QSet<QString> registeredToolNames;
    QSet<QString> loadedPluginKeys;

    for (const QString &rootPath : pluginSearchRoots()) {
        QDir root(rootPath);
        if (!root.exists()) {
            root.mkpath(".");
            continue;
        }

        const QFileInfoList manifests = root.entryInfoList(QStringList() << "*.json", QDir::Files | QDir::Readable, QDir::Name);
        for (const QFileInfo &manifestInfo : manifests) {
            PluginInfo plugin;
            plugin.manifestPath = manifestInfo.absoluteFilePath();

            QFile file(manifestInfo.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly)) {
                plugin.name = manifestInfo.baseName();
                const QString pluginKey = manifestPluginKey(manifestInfo);
                if (loadedPluginKeys.contains(pluginKey)) {
                    shadowed << QString("%1 (%2)").arg(plugin.name, manifestInfo.absoluteFilePath());
                    continue;
                }
                loadedPluginKeys.insert(pluginKey);
                plugin.errors << "Unreadable manifest";
                m_pluginInfos.push_back(plugin);
                errors << QString("%1 (unreadable)").arg(manifestInfo.fileName());
                continue;
            }

            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                plugin.name = manifestInfo.baseName();
                const QString pluginKey = manifestPluginKey(manifestInfo);
                if (loadedPluginKeys.contains(pluginKey)) {
                    shadowed << QString("%1 (%2)").arg(plugin.name, manifestInfo.absoluteFilePath());
                    continue;
                }
                loadedPluginKeys.insert(pluginKey);
                plugin.errors << "Invalid JSON";
                m_pluginInfos.push_back(plugin);
                errors << QString("%1 (invalid JSON)").arg(manifestInfo.fileName());
                continue;
            }

            const QJsonObject rootObject = document.object();
            const QString pluginKey = manifestPluginKey(manifestInfo, &rootObject);
            if (loadedPluginKeys.contains(pluginKey)) {
                shadowed << QString("%1 (%2)").arg(rootObject.value("name").toString(manifestInfo.baseName()).trimmed(),
                                                    manifestInfo.absoluteFilePath());
                continue;
            }
            loadedPluginKeys.insert(pluginKey);
            plugin.name = rootObject.value("name").toString(manifestInfo.baseName()).trimmed();
            if (plugin.name.isEmpty()) {
                plugin.name = manifestInfo.baseName();
                plugin.errors << "Missing plugin name";
            }
            plugin.trusted = m_trustedPlugins.contains(plugin.name);
            plugin.enabled = !m_disabledPlugins.contains(plugin.name);
            plugin.declaredScopes = toScopeList(rootObject.value("requested_scopes"));
            plugin.grantedScopes = m_grantedScopesByPlugin.value(normalizePluginKey(plugin.name));
            plugin.sandboxBackend = m_sandboxRunner ? m_sandboxRunner->backendName() : QStringLiteral("none");
            plugin.sandboxAvailable = m_sandboxRunner && m_sandboxRunner->isAvailable();
            plugin.sandboxStatus = m_sandboxRunner ? m_sandboxRunner->availabilityMessage() : QStringLiteral("No sandbox runner configured");

            const QJsonArray tools = rootObject.value("tools").toArray();
            if (tools.isEmpty()) {
                plugin.errors << "No tools declared";
            }

            int addedForPlugin = 0;
            for (const QJsonValue &toolValue : tools) {
                if (!toolValue.isObject()) {
                    plugin.errors << "A tool entry was not an object";
                    continue;
                }

                const QJsonObject obj = toolValue.toObject();
                ExternalTool tool;
                tool.pluginName = plugin.name;
                tool.toolName = obj.value("name").toString().trimmed();
                tool.description = obj.value("description").toString().trimmed();
                tool.command = obj.value("command").toString().trimmed();
                tool.workingDirectory = obj.value("working_directory").toString().trimmed();
                tool.timeoutMs = obj.value("timeout_ms").toInt(300000);

                const QString manifestDir = manifestInfo.absolutePath();
                if (!tool.workingDirectory.isEmpty()) {
                    QFileInfo workingInfo(tool.workingDirectory);
                    if (!workingInfo.isAbsolute()) {
                        tool.workingDirectory = QDir(manifestDir).absoluteFilePath(tool.workingDirectory);
                    }
                } else {
                    tool.workingDirectory = manifestDir;
                }
                tool.requiredScopes = normalizeScopes(plugin.declaredScopes + toScopeList(obj.value("scopes")));

                const QJsonArray argArray = obj.value("args").toArray();
                for (const QJsonValue &arg : argArray) {
                    QString argText = arg.toString();
                    if (!argText.isEmpty()) {
                        const QFileInfo argInfo(argText);
                        if (!argInfo.isAbsolute()) {
                            const QString candidate = QDir(manifestInfo.absolutePath()).absoluteFilePath(argText);
                            if (QFileInfo::exists(candidate)) {
                                argText = candidate;
                            }
                        }
                    }
                    tool.args << argText;
                }

                if (obj.value("input_schema").isObject()) {
                    tool.argumentSchema = QString::fromUtf8(QJsonDocument(obj.value("input_schema").toObject()).toJson(QJsonDocument::Compact));
                } else if (obj.value("input_schema").isString()) {
                    tool.argumentSchema = obj.value("input_schema").toString();
                }
                if (tool.argumentSchema.isEmpty()) {
                    tool.argumentSchema = R"({"type":"object"})";
                }

                PluginToolInfo toolInfo{tool.toolName, tool.description, tool.argumentSchema, tool.requiredScopes};
                plugin.tools.push_back(toolInfo);
                if (plugin.command.isEmpty() && !tool.command.isEmpty()) {
                    plugin.command = tool.command;
                    plugin.workingDirectory = tool.workingDirectory;
                    plugin.timeoutMs = tool.timeoutMs;
                }
                plugin.declaredScopes = normalizeScopes(plugin.declaredScopes + tool.requiredScopes);

                if (tool.toolName.isEmpty() || tool.description.isEmpty() || tool.command.isEmpty()) {
                    plugin.errors << QString("Tool '%1' is missing required fields").arg(tool.toolName.isEmpty() ? QStringLiteral("<unnamed>") : tool.toolName);
                    continue;
                }
                if (registeredToolNames.contains(tool.toolName)) {
                    plugin.errors << QString("Duplicate tool name skipped: %1").arg(tool.toolName);
                    continue;
                }

                QStringList missingForTool;
                for (const QString &scope : tool.requiredScopes) {
                    if (!plugin.grantedScopes.contains(scope)) {
                        missingForTool << scope;
                    }
                }
                plugin.missingScopes = normalizeScopes(plugin.missingScopes + missingForTool);

                if (plugin.trusted && plugin.enabled && missingForTool.isEmpty()) {
                    m_externalTools.push_back(tool);
                    registeredToolNames.insert(tool.toolName);
                    ++addedForPlugin;
                }
            }

            plugin.activeToolCount = addedForPlugin;
            plugin.valid = !plugin.tools.isEmpty();
            m_pluginInfos.push_back(plugin);

            if (addedForPlugin > 0) {
                loaded << QString("%1 (%2 active tool%3)").arg(plugin.name).arg(addedForPlugin).arg(addedForPlugin == 1 ? "" : "s");
            } else if (!plugin.valid || !plugin.errors.isEmpty()) {
                errors << QString("%1 (%2)").arg(manifestInfo.fileName(), plugin.errors.join(", "));
            }
        }
    }

    QStringList lines;
    lines << QString("Loaded %1 external tool%2.").arg(m_externalTools.size()).arg(m_externalTools.size() == 1 ? "" : "s");
    if (!loaded.isEmpty()) {
        lines << QString("Active plugins: %1").arg(loaded.join(", "));
    }
    const int inactiveCount = std::count_if(m_pluginInfos.begin(), m_pluginInfos.end(), [](const PluginInfo &plugin) {
        return plugin.valid && (!plugin.trusted || !plugin.enabled || !plugin.missingScopes.isEmpty());
    });
    if (inactiveCount > 0) {
        lines << QString("Inactive or partially granted plugins: %1").arg(inactiveCount);
    }
    if (!errors.isEmpty()) {
        lines << QString("Skipped or invalid: %1").arg(errors.join(", "));
    }
    if (!shadowed.isEmpty()) {
        lines << QString("Shadowed duplicate manifests: %1").arg(shadowed.join(", "));
    }
    if (m_sandboxRunner) {
        lines << QString("Plugin sandbox: %1 (%2)").arg(m_sandboxRunner->backendName(), m_sandboxRunner->availabilityMessage());
    }
    lines << QString("Plugin directory: %1").arg(pluginDirectory());
    return lines.join('\n');
}

ToolResult ToolRegistry::runGetCurrentTime() const
{
    ToolResult result;
    result.success = true;
    const QDateTime local = QDateTime::currentDateTime();
    result.output = QString("Current local time: %1").arg(local.toString(Qt::ISODate));
    return result;
}

ToolResult ToolRegistry::runListDirectory(const QJsonObject &arguments) const
{
    QString error;
    const QString path = validatePathArgument(arguments, "path", false, &error);
    if (path.isEmpty()) {
        return {false, error};
    }

    QDir dir(path);
    if (!dir.exists()) {
        return {false, QStringLiteral("Directory does not exist: %1").arg(sanitizeDisplayPath(path))};
    }

    const QFileInfoList entries = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries,
                                                    QDir::DirsFirst | QDir::Name);

    QStringList lines;
    lines << QString("Directory: %1").arg(sanitizeDisplayPath(path));
    for (const QFileInfo &entry : entries.mid(0, 200)) {
        lines << QString("- %1 %2").arg(entry.isDir() ? "[DIR]" : "[FILE]", entry.fileName());
    }
    if (entries.size() > 200) {
        lines << QString("... %1 more entries omitted").arg(entries.size() - 200);
    }

    return {true, lines.join('\n')};
}

ToolResult ToolRegistry::runReadTextFile(const QJsonObject &arguments) const
{
    QString error;
    const QString path = validatePathArgument(arguments, "path", false, &error);
    if (path.isEmpty()) {
        return {false, error};
    }

    QFile file(path);
    if (!file.exists()) {
        return {false, QStringLiteral("File does not exist: %1").arg(sanitizeDisplayPath(path))};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return {false, QStringLiteral("Unable to open file for reading: %1").arg(sanitizeDisplayPath(path))};
    }
    if (file.size() > kMaxReadBytes) {
        return {false, QStringLiteral("File exceeds read limit of 256 KB: %1").arg(sanitizeDisplayPath(path))};
    }

    const QString content = QString::fromUtf8(file.readAll());
    return {true, QString("File: %1\n\n%2").arg(sanitizeDisplayPath(path), content)};
}

ToolResult ToolRegistry::runWriteTextFile(const QJsonObject &arguments) const
{
    QString error;
    const QString path = validatePathArgument(arguments, "path", true, &error);
    if (path.isEmpty()) {
        return {false, error};
    }

    const QString content = arguments.value("content").toString();
    if (content.toUtf8().size() > kMaxWriteBytes) {
        return {false, QStringLiteral("Content exceeds write limit of 256 KB.")};
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {false, QStringLiteral("Unable to open file for writing: %1").arg(sanitizeDisplayPath(path))};
    }
    file.write(content.toUtf8());
    file.close();

    return {true, QStringLiteral("Wrote file: %1").arg(sanitizeDisplayPath(path))};
}

ToolResult ToolRegistry::runExternalTool(const ExternalTool &tool, const QJsonObject &arguments) const
{
    if (!m_sandboxRunner) {
        return {false, QStringLiteral("No sandbox runner is configured for external plugins.")};
    }
    if (!m_sandboxRunner->isAvailable()) {
        return {false, QStringLiteral("External plugin execution is blocked because %1.").arg(m_sandboxRunner->availabilityMessage())};
    }

    QJsonArray scopes;
    for (const QString &scope : tool.requiredScopes) {
        scopes.append(scope);
    }
    const QJsonObject payload{
        {"tool", tool.toolName},
        {"plugin", tool.pluginName},
        {"arguments", arguments},
        {"granted_scopes", scopes}
    };

    SandboxRequest request;
    request.pluginName = tool.pluginName;
    request.command = tool.command;
    request.args = tool.args;
    request.workingDirectory = tool.workingDirectory;
    request.grantedScopes = tool.requiredScopes;
    request.timeoutMs = tool.timeoutMs;
    request.payload = payload;

    if (tool.requiredScopes.contains(QStringLiteral("filesystem.write"))) {
        request.readWritePaths = allowedRoots();
    } else if (tool.requiredScopes.contains(QStringLiteral("filesystem.read"))) {
        request.readOnlyPaths = allowedRoots();
    }

    const SandboxResult sandboxResult = m_sandboxRunner->run(request);
    if (!sandboxResult.started && !sandboxResult.errorMessage.isEmpty()) {
        return {false, QStringLiteral("Sandbox startup failed for %1: %2").arg(tool.toolName, sandboxResult.errorMessage)};
    }
    if (!sandboxResult.finished && !sandboxResult.errorMessage.isEmpty()) {
        return {false, QStringLiteral("Sandbox execution failed for %1: %2").arg(tool.toolName, sandboxResult.errorMessage)};
    }

    const QByteArray rawStdout = sandboxResult.stdoutText.toUtf8();
    const QByteArray rawStderr = sandboxResult.stderrText.toUtf8();
    if (rawStdout.size() > kMaxProcessOutputBytes) {
        return {false, QString("Plugin tool output exceeded %1 bytes: %2").arg(kMaxProcessOutputBytes).arg(tool.toolName)};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(rawStdout, &parseError);
    if (parseError.error == QJsonParseError::NoError && document.isObject()) {
        const QJsonObject obj = document.object();
        const bool success = obj.value("success").toBool(sandboxResult.success);
        QString output = obj.value("output").toString();
        if (output.isEmpty()) {
            output = prettyJson(obj.value("result"));
        }
        if (!rawStderr.trimmed().isEmpty()) {
            output += QString("\n\n[stderr]\n%1").arg(QString::fromUtf8(rawStderr.left(8192)));
        }
        if (!sandboxResult.sandboxed) {
            output.prepend("[warning] Plugin did not run inside a sandbox.\n\n");
        }
        return {success, output.trimmed()};
    }

    QString output = QString::fromUtf8(rawStdout);
    if (!rawStderr.trimmed().isEmpty()) {
        if (!output.isEmpty()) {
            output += "\n\n";
        }
        output += QString("[stderr]\n%1").arg(QString::fromUtf8(rawStderr.left(8192)));
    }
    if (output.trimmed().isEmpty()) {
        output = QString("Plugin exited with code %1 and returned no output.").arg(sandboxResult.exitCode);
    }
    if (!sandboxResult.sandboxed) {
        output.prepend("[warning] Plugin did not run inside a sandbox.\n\n");
    }
    return {sandboxResult.success, output.trimmed()};
}

QString ToolRegistry::validatePathArgument(const QJsonObject &arguments,
                                           const QString &key,
                                           bool requireExistingParent,
                                           QString *errorMessage) const
{
    const QString rawPath = arguments.value(key).toString().trimmed();
    if (rawPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Missing path argument: %1").arg(key);
        }
        return QString();
    }

    QFileInfo info(rawPath);
    QString cleanPath = info.isAbsolute()
        ? QDir::cleanPath(info.absoluteFilePath())
        : QDir::cleanPath(QDir::home().absoluteFilePath(rawPath));

    if (!isPathAllowed(cleanPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Path is outside the allowed scope: %1").arg(sanitizeDisplayPath(cleanPath));
        }
        return QString();
    }

    if (requireExistingParent) {
        QFileInfo parentInfo(QFileInfo(cleanPath).absolutePath());
        if (!parentInfo.exists() || !parentInfo.isDir()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Parent directory does not exist: %1").arg(sanitizeDisplayPath(parentInfo.absoluteFilePath()));
            }
            return QString();
        }
    }

    return cleanPath;
}

bool ToolRegistry::isPathAllowed(const QString &path) const
{
    for (const QString &root : m_allowedRoots) {
        if (path == root || path.startsWith(root + '/')) {
            return true;
        }
    }
    return false;
}

QString ToolRegistry::sanitizeDisplayPath(const QString &path) const
{
    QString display = path;
    const QString home = QDir::homePath();
    if (display.startsWith(home)) {
        display.replace(0, home.size(), QStringLiteral("~"));
    }
    return display;
}

} // namespace llm_gui::tools
