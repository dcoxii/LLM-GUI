#pragma once

#include <QMainWindow>
#include <memory>
#include <QVector>
#include "core/ChatSession.h"
#include "core/AppSettings.h"
#include "tools/ToolRegistry.h"
#include "util/AttachmentProcessor.h"

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTextBrowser;
class QTextEdit;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QDragEnterEvent;
class QDropEvent;

namespace llm_gui::core {
class AppSettings;
class ChatSession;
class SessionStore;
}

namespace llm_gui::services {
class ProviderClient;
class DeepSeekClient;
class LlamaCppClient;
class ProviderHealthProbe;
struct ProviderHealth;
}

namespace llm_gui::app {

class PtyTerminal;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(llm_gui::core::AppSettings &settings,
                        llm_gui::core::SessionStore &sessionStore,
                        QWidget *parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void sendPrompt();
    void stopPrompt();
    void openSettings();
    void attachFiles();
    void clearAttachments();
    void exportLastAssistantReply();
    void reloadPlugins();
    void managePlugins();
    void appendAssistantToken(const QString &token);
    void finalizeAssistantMessage();
    void requestFailed(const QString &message);
    void refreshClientSettings();
    void createNewSession();
    void saveCurrentSession();
    void loadSelectedSession();
    void deleteSelectedSession();
    void runProviderHealthChecks();
    void updateProviderHealth(const llm_gui::services::ProviderHealth &health);
    void refreshAvailableModels();
    void applySelectedModel();

private:
    void appendMessage(const QString &role, const QString &content);
    QString currentPrompt() const;
    QString promptWithAttachments(const QString &prompt) const;
    QString buildEffectivePrompt(const QString &prompt) const;
    QString buildToolConversation(const QString &prompt) const;
    QString lastAssistantMessage() const;
    void refreshAttachmentStatus();
    void addAttachmentsFromPaths(const QStringList &paths);
    void setBusy(bool busy);
    void refreshTranscriptView();
    void reloadSessionList();
    void bindProvider(llm_gui::services::ProviderClient *client);
    llm_gui::services::ProviderClient *activeProvider() const;
    void ensureSessionInitialized();
    void updateWindowTitle();
    void runToolEnabledPrompt(const QString &prompt, const QString &visiblePrompt);
    void showPluginReloadStatus(const QString &message);
    void populateModelSelector(const QStringList &models, const QString &selectedModel);
    void populateLlamaCppModelSelector();
    void autoConfigureLlamaCppModel();

    llm_gui::core::AppSettings &m_settings;
    llm_gui::core::SessionStore &m_sessionStore;
    llm_gui::services::DeepSeekClient *m_deepSeekClient;
    llm_gui::services::LlamaCppClient *m_llamaCppClient;
    llm_gui::services::ProviderHealthProbe *m_healthProbe;
    llm_gui::services::ProviderClient *m_activeClient;

    QListWidget *m_sessionList;
    QTextBrowser *m_transcript;
    QTextEdit *m_promptEditor;
    QCheckBox *m_enableToolsCheckBox;
    QPushButton *m_attachButton;
    QPushButton *m_clearAttachmentsButton;
    QPushButton *m_exportReplyButton;
    QPushButton *m_sendButton;
    QPushButton *m_stopButton;
    QPushButton *m_newSessionButton;
    QPushButton *m_saveSessionButton;
    QPushButton *m_deleteSessionButton;
    QComboBox *m_modelCombo;
    QLabel *m_attachmentLabel;
    QLabel *m_statusLabel;
    QLabel *m_healthLabel;
    QDockWidget *m_terminalDock;
    PtyTerminal *m_terminal;

    QString m_activeAssistantBuffer;
    QVector<llm_gui::util::ProcessedAttachment> m_pendingAttachments;
    QString m_deepSeekHealth;
    QString m_llamaCppHealth;
    bool m_busy { false };
    llm_gui::tools::ToolRegistry m_toolRegistry;

    std::unique_ptr<llm_gui::core::ChatSession> m_currentSession;
};

} // namespace llm_gui::app
