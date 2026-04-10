#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>

namespace llm_gui::tools {

struct SandboxRequest
{
    QString pluginName;
    QString command;
    QStringList args;
    QString workingDirectory;
    QStringList grantedScopes;
    QStringList readOnlyPaths;
    QStringList readWritePaths;
    int timeoutMs { 300000 };
    QJsonObject payload;
};

struct SandboxResult
{
    bool started { false };
    bool finished { false };
    bool success { false };
    bool sandboxed { false };
    int exitCode { -1 };
    QString stdoutText;
    QString stderrText;
    QString errorMessage;
};

class SandboxRunner
{
public:
    virtual ~SandboxRunner() = default;

    virtual QString backendName() const = 0;
    virtual bool isAvailable() const = 0;
    virtual QString availabilityMessage() const = 0;
    virtual SandboxResult run(const SandboxRequest &request) const = 0;
};

class BubblewrapRunner final : public SandboxRunner
{
public:
    BubblewrapRunner();

    QString backendName() const override;
    bool isAvailable() const override;
    QString availabilityMessage() const override;
    SandboxResult run(const SandboxRequest &request) const override;

private:
    QString m_bwrapPath;
};

} // namespace llm_gui::tools
