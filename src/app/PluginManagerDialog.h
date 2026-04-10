#pragma once

#include <QDialog>
#include <QStringList>

class QLabel;
class QPushButton;
class QTableWidget;
class QTextBrowser;

namespace llm_gui::core {
class AppSettings;
}

namespace llm_gui::tools {
class ToolRegistry;
}

namespace llm_gui::app {

class PluginManagerDialog : public QDialog
{
    Q_OBJECT

public:
    PluginManagerDialog(llm_gui::core::AppSettings &settings,
                        llm_gui::tools::ToolRegistry &toolRegistry,
                        QWidget *parent = nullptr);

private slots:
    void refreshFromRegistry();
    void updateDetails();
    void saveAndApply();
    void importManifest();
    void openPluginFolder();
    void reloadPlugins();
    void grantRequestedScopes();
    void revokePluginScopes();

private:
    QStringList trustedPluginsFromTable() const;
    QStringList disabledPluginsFromTable() const;
    QStringList grantedScopesFromRow(int row) const;
    void populateTable();

    llm_gui::core::AppSettings &m_settings;
    llm_gui::tools::ToolRegistry &m_toolRegistry;

    QLabel *m_folderLabel;
    QTableWidget *m_table;
    QTextBrowser *m_details;
    QPushButton *m_reloadButton;
    QPushButton *m_openFolderButton;
    QPushButton *m_importButton;
    QPushButton *m_grantScopesButton;
    QPushButton *m_revokeScopesButton;
    QPushButton *m_saveButton;
};

} // namespace llm_gui::app
