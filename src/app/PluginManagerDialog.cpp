#include "app/PluginManagerDialog.h"

#include "core/AppSettings.h"
#include "tools/ToolRegistry.h"

#include <QDesktopServices>
#include <QDir>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace llm_gui::app {
namespace {
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
}

PluginManagerDialog::PluginManagerDialog(llm_gui::core::AppSettings &settings,
                                         llm_gui::tools::ToolRegistry &toolRegistry,
                                         QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_toolRegistry(toolRegistry)
    , m_folderLabel(new QLabel(this))
    , m_table(new QTableWidget(this))
    , m_details(new QTextBrowser(this))
    , m_reloadButton(new QPushButton("Reload", this))
    , m_openFolderButton(new QPushButton("Open Plugin Folder", this))
    , m_importButton(new QPushButton("Import Manifest", this))
    , m_grantScopesButton(new QPushButton("Grant Requested Scopes", this))
    , m_revokeScopesButton(new QPushButton("Revoke Scopes", this))
    , m_saveButton(new QPushButton("Save", this))
{
    setWindowTitle("Plugin Manager");
    resize(1080, 680);

    auto *layout = new QVBoxLayout(this);
    auto *topRow = new QHBoxLayout();
    topRow->addWidget(m_folderLabel, 1);
    topRow->addWidget(m_reloadButton);
    topRow->addWidget(m_openFolderButton);
    topRow->addWidget(m_importButton);
    topRow->addWidget(m_grantScopesButton);
    topRow->addWidget(m_revokeScopesButton);

    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({"Plugin", "Trusted", "Enabled", "Granted scopes", "Status", "Tools"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::AllEditTriggers);

    m_details->setOpenExternalLinks(true);

    auto *buttons = new QDialogButtonBox(Qt::Horizontal, this);
    buttons->addButton(m_saveButton, QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Close);

    layout->addLayout(topRow);
    layout->addWidget(new QLabel("New plugins are visible first, but tools stay inactive until the plugin is trusted, enabled, and granted every required scope.", this));
    layout->addWidget(new QLabel(QString("Available scopes: %1").arg(llm_gui::tools::ToolRegistry::availablePluginScopes().join(", ")), this));
    layout->addWidget(m_table, 2);
    layout->addWidget(new QLabel("Details", this));
    layout->addWidget(m_details, 1);
    layout->addWidget(buttons);

    connect(m_reloadButton, &QPushButton::clicked, this, &PluginManagerDialog::reloadPlugins);
    connect(m_openFolderButton, &QPushButton::clicked, this, &PluginManagerDialog::openPluginFolder);
    connect(m_importButton, &QPushButton::clicked, this, &PluginManagerDialog::importManifest);
    connect(m_grantScopesButton, &QPushButton::clicked, this, &PluginManagerDialog::grantRequestedScopes);
    connect(m_revokeScopesButton, &QPushButton::clicked, this, &PluginManagerDialog::revokePluginScopes);
    connect(m_saveButton, &QPushButton::clicked, this, &PluginManagerDialog::saveAndApply);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &PluginManagerDialog::updateDetails);
    connect(m_table, &QTableWidget::itemChanged, this, &PluginManagerDialog::updateDetails);

    refreshFromRegistry();
}

void PluginManagerDialog::refreshFromRegistry()
{
    m_folderLabel->setText(QString("Plugin folder: %1").arg(m_toolRegistry.pluginDirectory()));
    populateTable();
}

void PluginManagerDialog::populateTable()
{
    const auto plugins = m_toolRegistry.pluginInfos();
    m_table->blockSignals(true);
    m_table->setRowCount(plugins.size());

    for (int row = 0; row < plugins.size(); ++row) {
        const auto &plugin = plugins.at(row);
        auto *nameItem = new QTableWidgetItem(plugin.name);
        nameItem->setData(Qt::UserRole, plugin.name);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        nameItem->setToolTip(plugin.manifestPath);
        m_table->setItem(row, 0, nameItem);

        auto *trustedItem = new QTableWidgetItem();
        trustedItem->setFlags((trustedItem->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        trustedItem->setCheckState(plugin.trusted ? Qt::Checked : Qt::Unchecked);
        trustedItem->setToolTip("Trusted plugins are allowed to execute local commands.");
        m_table->setItem(row, 1, trustedItem);

        auto *enabledItem = new QTableWidgetItem();
        enabledItem->setFlags((enabledItem->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        enabledItem->setCheckState(plugin.enabled ? Qt::Checked : Qt::Unchecked);
        enabledItem->setToolTip("Enabled plugins expose eligible tools to the LLM.");
        m_table->setItem(row, 2, enabledItem);

        auto *scopesItem = new QTableWidgetItem(plugin.grantedScopes.join(", "));
        scopesItem->setToolTip("Comma-separated granted scopes for this plugin.");
        m_table->setItem(row, 3, scopesItem);

        QString status = plugin.valid ? "Ready" : "Invalid";
        if (!plugin.valid && !plugin.errors.isEmpty()) {
            status = QString("Invalid (%1)").arg(plugin.errors.first());
        } else if (!plugin.trusted) {
            status = "Needs trust";
        } else if (!plugin.enabled) {
            status = "Disabled";
        } else if (!plugin.missingScopes.isEmpty()) {
            QStringList labeledScopes;
            for (const QString &scope : plugin.missingScopes) {
                labeledScopes << llm_gui::tools::ToolRegistry::scopeLabel(scope);
            }
            status = QString("Needs scopes: %1").arg(labeledScopes.join(", "));
        } else if (plugin.activeToolCount < plugin.tools.size()) {
            status = QString("Partial (%1/%2 tools)").arg(plugin.activeToolCount).arg(plugin.tools.size());
        }
        if (plugin.sandboxAvailable) {
            status += QString(" | sandbox: %1").arg(plugin.sandboxBackend);
        } else if (!plugin.sandboxStatus.isEmpty()) {
            status += QString(" | sandbox unavailable");
        }
        auto *statusItem = new QTableWidgetItem(status);
        statusItem->setFlags(statusItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 4, statusItem);

        QStringList toolNames;
        for (const auto &tool : plugin.tools) {
            toolNames << tool.name;
        }
        auto *toolsItem = new QTableWidgetItem(toolNames.join(", "));
        toolsItem->setFlags(toolsItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 5, toolsItem);
    }

    m_table->blockSignals(false);
    if (m_table->rowCount() > 0) {
        m_table->selectRow(0);
    } else {
        m_details->setHtml("<p>No plugin manifests found yet.</p>");
    }
    updateDetails();
}

QStringList PluginManagerDialog::trustedPluginsFromTable() const
{
    QStringList values;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const auto *nameItem = m_table->item(row, 0);
        const auto *trustedItem = m_table->item(row, 1);
        if (nameItem && trustedItem && trustedItem->checkState() == Qt::Checked) {
            values << nameItem->data(Qt::UserRole).toString();
        }
    }
    values.removeDuplicates();
    values.sort();
    return values;
}

QStringList PluginManagerDialog::disabledPluginsFromTable() const
{
    QStringList values;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const auto *nameItem = m_table->item(row, 0);
        const auto *enabledItem = m_table->item(row, 2);
        if (nameItem && enabledItem && enabledItem->checkState() != Qt::Checked) {
            values << nameItem->data(Qt::UserRole).toString();
        }
    }
    values.removeDuplicates();
    values.sort();
    return values;
}

QStringList PluginManagerDialog::grantedScopesFromRow(int row) const
{
    if (row < 0) {
        return {};
    }
    auto *item = m_table->item(row, 3);
    if (!item) {
        return {};
    }
    return normalizeScopes(item->text().split(',', Qt::SkipEmptyParts));
}

void PluginManagerDialog::updateDetails()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return;
    }

    const QString pluginName = m_table->item(row, 0)->data(Qt::UserRole).toString();
    const auto plugins = m_toolRegistry.pluginInfos();
    for (const auto &plugin : plugins) {
        if (plugin.name != pluginName) {
            continue;
        }

        const bool trusted = m_table->item(row, 1)->checkState() == Qt::Checked;
        const bool enabled = m_table->item(row, 2)->checkState() == Qt::Checked;
        const QStringList grantedScopes = grantedScopesFromRow(row);
        QStringList missingScopes;
        for (const QString &scope : plugin.declaredScopes) {
            if (!grantedScopes.contains(scope)) {
                missingScopes << scope;
            }
        }

        QString html;
        html += QString("<h3>%1</h3>").arg(plugin.name.toHtmlEscaped());
        html += QString("<p><b>Manifest:</b> %1</p>").arg(plugin.manifestPath.toHtmlEscaped());
        html += QString("<p><b>Command:</b> %1</p>").arg(plugin.command.toHtmlEscaped());
        if (!plugin.workingDirectory.isEmpty()) {
            html += QString("<p><b>Working directory:</b> %1</p>").arg(plugin.workingDirectory.toHtmlEscaped());
        }
        html += QString("<p><b>Timeout:</b> %1 ms</p>").arg(plugin.timeoutMs);
        html += QString("<p><b>Sandbox:</b> %1<br><b>Sandbox status:</b> %2</p>").arg(plugin.sandboxBackend.toHtmlEscaped(), plugin.sandboxStatus.toHtmlEscaped());
        html += QString("<p><b>Trust:</b> %1<br><b>Enabled:</b> %2</p>")
            .arg(trusted ? "Trusted" : "Not trusted")
            .arg(enabled ? "Enabled" : "Disabled");
        auto labeledScopes = [](const QStringList &scopes) -> QString {
            if (scopes.isEmpty()) return QStringLiteral("None");
            QStringList labeled;
            for (const QString &s : scopes)
                labeled << QString("%1 (%2)").arg(llm_gui::tools::ToolRegistry::scopeLabel(s), s);
            return labeled.join(", ").toHtmlEscaped();
        };

        html += QString("<p><b>Declared scopes:</b> %1<br><b>Granted scopes:</b> %2</p>")
            .arg(labeledScopes(plugin.declaredScopes))
            .arg(labeledScopes(grantedScopes));
        if (!missingScopes.isEmpty()) {
            html += QString("<p><b>Missing scopes:</b> %1</p>").arg(labeledScopes(missingScopes));
        }

        if (!plugin.errors.isEmpty()) {
            html += QString("<p><b>Manifest issues:</b><br>%1</p>").arg(plugin.errors.join("<br>").toHtmlEscaped());
        }

        html += "<p><b>Tools</b></p><ul>";
        for (const auto &tool : plugin.tools) {
            const QString toolScopes = tool.requiredScopes.isEmpty()
                ? QStringLiteral("none")
                : labeledScopes(tool.requiredScopes);
            html += QString("<li><b>%1</b>: %2<br><b>Scopes:</b> %3<pre>%4</pre></li>")
                .arg(tool.name.toHtmlEscaped(),
                     tool.description.toHtmlEscaped(),
                     toolScopes,
                     tool.argumentSchema.toHtmlEscaped());
        }
        html += "</ul>";
        html += "<p><b>Warning:</b> External plugins now run through a sandbox runner when available. Scope gating still does not make an untrusted executable safe by itself, so only trust plugins you would run locally.</p>";

        m_details->setHtml(html);
        return;
    }
}

void PluginManagerDialog::saveAndApply()
{
    const QStringList trusted = trustedPluginsFromTable();
    const QStringList disabled = disabledPluginsFromTable();

    m_settings.setTrustedPlugins(trusted);
    m_settings.setDisabledPlugins(disabled);
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QString pluginName = m_table->item(row, 0)->data(Qt::UserRole).toString();
        m_settings.setGrantedPluginScopes(pluginName, grantedScopesFromRow(row));
    }

    m_toolRegistry.setPluginPolicy(trusted, disabled, m_settings.allGrantedPluginScopes());
    const QString summary = m_toolRegistry.reloadPlugins();
    refreshFromRegistry();

    QMessageBox::information(this,
                             "Plugins applied",
                             QString("Saved plugin policy.\n\n%1").arg(summary));
}

void PluginManagerDialog::importManifest()
{
    const QString sourcePath = QFileDialog::getOpenFileName(
        this,
        "Import Plugin Manifest",
        QString(),
        "Plugin manifest (*.json);;All files (*)");

    if (sourcePath.isEmpty()) {
        return;
    }

    const QString targetDir = m_toolRegistry.pluginDirectory();
    if (targetDir.isEmpty()) {
        QMessageBox::warning(this, "Import failed", "The plugin directory could not be determined.");
        return;
    }

    QDir().mkpath(targetDir);
    const QString targetPath = QDir(targetDir).filePath(QFileInfo(sourcePath).fileName());

    if (QFile::exists(targetPath)) {
        const auto answer = QMessageBox::question(this,
                                                  "Overwrite manifest",
                                                  QString("Replace existing manifest?\n%1").arg(targetPath));
        if (answer != QMessageBox::Yes) {
            return;
        }
        QFile::remove(targetPath);
    }

    if (!QFile::copy(sourcePath, targetPath)) {
        QMessageBox::warning(this,
                             "Import failed",
                             QString("Could not copy manifest to %1").arg(targetPath));
        return;
    }

    QMessageBox::information(this,
                             "Manifest imported",
                             "The manifest was copied into the plugin folder. If it depends on scripts or binaries, make sure those files are reachable from the manifest paths.");
    reloadPlugins();
}

void PluginManagerDialog::openPluginFolder()
{
    const QString path = m_toolRegistry.pluginDirectory();
    if (path.isEmpty()) {
        QMessageBox::warning(this, "Open failed", "The plugin directory could not be determined.");
        return;
    }

    QDir().mkpath(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void PluginManagerDialog::reloadPlugins()
{
    m_toolRegistry.setPluginPolicy(m_settings.trustedPlugins(),
                                   m_settings.disabledPlugins(),
                                   m_settings.allGrantedPluginScopes());
    const QString summary = m_toolRegistry.reloadPlugins();
    refreshFromRegistry();
    QMessageBox::information(this, "Plugins reloaded", summary);
}

void PluginManagerDialog::grantRequestedScopes()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return;
    }
    const QString pluginName = m_table->item(row, 0)->data(Qt::UserRole).toString();
    for (const auto &plugin : m_toolRegistry.pluginInfos()) {
        if (plugin.name == pluginName) {
            m_table->item(row, 3)->setText(plugin.declaredScopes.join(", "));
            break;
        }
    }
    updateDetails();
}

void PluginManagerDialog::revokePluginScopes()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return;
    }
    m_table->item(row, 3)->setText(QString());
    updateDetails();
}

} // namespace llm_gui::app
