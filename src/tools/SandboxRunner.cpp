#include "tools/SandboxRunner.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace llm_gui::tools {
namespace {
constexpr int kStartTimeoutMs = 3000;
constexpr int kKillWaitMs = 1000;
constexpr int kMaxOutputBytes = 512 * 1024;

QString cleanExistingPath(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        return {};
    }
    const QString clean = QDir::cleanPath(path);
    return QFileInfo::exists(clean) ? clean : QString();
}

QStringList uniquePaths(QStringList paths)
{
    for (QString &path : paths) {
        path = QDir::cleanPath(path.trimmed());
    }
    paths.removeAll(QString());
    paths.removeDuplicates();
    paths.sort();
    return paths;
}

void addSystemBind(QStringList &args, const QString &path)
{
    const QString clean = cleanExistingPath(path);
    if (clean.isEmpty()) {
        return;
    }
    args << "--ro-bind" << clean << clean;
}

QString resolvedWorkingDirectory(const SandboxRequest &request)
{
    if (!request.workingDirectory.trimmed().isEmpty()) {
        return QDir::cleanPath(request.workingDirectory.trimmed());
    }

    QFileInfo commandInfo(request.command);
    if (commandInfo.isAbsolute()) {
        return commandInfo.absolutePath();
    }

    return QDir::currentPath();
}
}

BubblewrapRunner::BubblewrapRunner()
    : m_bwrapPath(QStandardPaths::findExecutable("bwrap"))
{
}

QString BubblewrapRunner::backendName() const
{
    return QStringLiteral("bubblewrap");
}

bool BubblewrapRunner::isAvailable() const
{
    return !m_bwrapPath.isEmpty();
}

QString BubblewrapRunner::availabilityMessage() const
{
    if (isAvailable()) {
        return QStringLiteral("bubblewrap available at %1").arg(m_bwrapPath);
    }
    return QStringLiteral("bubblewrap (bwrap) is not installed or not on PATH");
}

SandboxResult BubblewrapRunner::run(const SandboxRequest &request) const
{
    SandboxResult result;
    result.sandboxed = true;

    if (!isAvailable()) {
        result.errorMessage = availabilityMessage();
        return result;
    }

    QStringList args;
    args << "--die-with-parent";
    args << "--new-session";
    args << "--proc" << "/proc";
    args << "--dev" << "/dev";
    args << "--tmpfs" << "/tmp";
    args << "--clearenv";
    args << "--setenv" << "PATH" << "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
    args << "--setenv" << "HOME" << QDir::homePath();
    args << "--setenv" << "PWD" << resolvedWorkingDirectory(request);

    addSystemBind(args, "/usr");
    addSystemBind(args, "/bin");
    addSystemBind(args, "/sbin");
    addSystemBind(args, "/lib");
    addSystemBind(args, "/lib64");
    addSystemBind(args, "/etc");
    addSystemBind(args, "/opt");
    addSystemBind(args, "/run/current-system/sw");

    if (!request.grantedScopes.contains(QStringLiteral("network"))) {
        args << "--unshare-net";
    }

    QStringList readOnlyPaths = uniquePaths(request.readOnlyPaths);
    QStringList readWritePaths = uniquePaths(request.readWritePaths);

    const QString workingDir = resolvedWorkingDirectory(request);
    if (!workingDir.isEmpty()) {
        if (readWritePaths.contains(workingDir)) {
            args << "--bind" << workingDir << workingDir;
        } else {
            args << "--ro-bind" << workingDir << workingDir;
        }
        args << "--chdir" << workingDir;
    }

    QFileInfo commandInfo(request.command);
    if (commandInfo.isAbsolute()) {
        const QString cmdPath = QDir::cleanPath(commandInfo.absoluteFilePath());
        if (QFileInfo::exists(cmdPath)) {
            const QString cmdDir = commandInfo.absolutePath();
            if (!readOnlyPaths.contains(cmdDir) && !readWritePaths.contains(cmdDir)) {
                readOnlyPaths << cmdDir;
                readOnlyPaths = uniquePaths(readOnlyPaths);
            }
        }
    }

    for (const QString &path : readOnlyPaths) {
        if (QFileInfo::exists(path)) {
            args << "--ro-bind" << path << path;
        }
    }
    for (const QString &path : readWritePaths) {
        if (QFileInfo::exists(path)) {
            args << "--bind" << path << path;
        }
    }

    args << request.command;
    args << request.args;

    QProcess process;
    process.start(m_bwrapPath, args);
    if (!process.waitForStarted(kStartTimeoutMs)) {
        result.errorMessage = QStringLiteral("Sandboxed plugin failed to start with bubblewrap.");
        return result;
    }
    result.started = true;

    process.write(QJsonDocument(request.payload).toJson(QJsonDocument::Compact));
    process.closeWriteChannel();

    if (!process.waitForFinished(request.timeoutMs)) {
        process.kill();
        process.waitForFinished(kKillWaitMs);
        result.finished = true;
        result.exitCode = process.exitCode();
        result.stdoutText = QString::fromUtf8(process.readAllStandardOutput().left(kMaxOutputBytes));
        result.stderrText = QString::fromUtf8(process.readAllStandardError().left(kMaxOutputBytes));
        result.errorMessage = QStringLiteral("Sandboxed plugin timed out after %1 ms.").arg(request.timeoutMs);
        return result;
    }

    result.finished = true;
    result.exitCode = process.exitCode();
    result.stdoutText = QString::fromUtf8(process.readAllStandardOutput().left(kMaxOutputBytes));
    result.stderrText = QString::fromUtf8(process.readAllStandardError().left(kMaxOutputBytes));
    result.success = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    return result;
}

} // namespace llm_gui::tools
