#include "app/MainWindow.h"

#include "app/PluginManagerDialog.h"
#include "app/PtyTerminal.h"

#include <algorithm>

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDateTime>
#include <QDialog>
#include <QFile>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QShortcut>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextEdit>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include "app/SettingsDialog.h"
#include "core/AppSettings.h"
#include "core/ChatSession.h"
#include "core/SessionStore.h"
#include "services/DeepSeekClient.h"
#include "services/LlamaCppClient.h"
#include "services/ProviderClient.h"
#include "services/ProviderHealthProbe.h"
#include "util/AttachmentProcessor.h"
#include "util/TranscriptFormatter.h"

namespace llm_gui::app {

namespace {
constexpr int kMaxToolIterations = 6;
constexpr int kMaxPromptHistoryMessages = 8;
constexpr int kMaxPromptHistoryChars = 12000;

QString llamaModelLabelForPath(const QString &modelPath)
{
    const QFileInfo info(modelPath.trimmed());
    const QString baseName = info.completeBaseName().trimmed();
    return baseName.isEmpty() ? info.fileName().trimmed() : baseName;
}

QString llamaDisplayLabelForPath(const QString &modelPath, const QHash<QString, int> &labelCounts)
{
    const QFileInfo info(modelPath.trimmed());
    const QString baseLabel = llamaModelLabelForPath(modelPath);
    if (baseLabel.isEmpty()) {
        return info.fileName().trimmed();
    }

    if (labelCounts.value(baseLabel) <= 1) {
        return baseLabel;
    }

    const QString parentName = info.dir().dirName().trimmed();
    if (!parentName.isEmpty()) {
        return QStringLiteral("%1 — %2").arg(baseLabel, parentName);
    }

    return QStringLiteral("%1 — %2").arg(baseLabel, info.absolutePath());
}

QString llamaBackendUnavailableMessage()
{
    return QStringLiteral("Embedded llama.cpp support is not available in this build. Falling back to ChatGPT/OpenAI.");
}

QString promptRoleLabel(const QString &role)
{
    if (role == QStringLiteral("user")) return QStringLiteral("User");
    if (role == QStringLiteral("assistant")) return QStringLiteral("Assistant");
    if (role == QStringLiteral("tool")) return QStringLiteral("Tool");
    return role.isEmpty() ? QStringLiteral("Message") : role;
}

QString formatRecentMessagesForPrompt(const std::unique_ptr<llm_gui::core::ChatSession> &session,
                                      int maxMessages,
                                      int maxChars,
                                      int excludeTrailingMessages = 0)
{
    if (!session || session->messages.isEmpty() || maxMessages <= 0 || maxChars <= 0) {
        return {};
    }

    const int totalMessages = std::max(0, static_cast<int>(session->messages.size()) - std::max(0, excludeTrailingMessages));
    if (totalMessages <= 0) {
        return {};
    }

    const int startIndex = std::max(0, totalMessages - maxMessages);
    QStringList blocks;
    for (int index = startIndex; index < totalMessages; ++index) {
        const auto &message = session->messages.at(index);
        QString content = message.content.trimmed();
        if (content.isEmpty()) {
            continue;
        }

        blocks << QStringLiteral("%1:\n%2").arg(promptRoleLabel(message.role), content);
    }

    QString history = blocks.join(QStringLiteral("\n\n"));
    if (history.size() > maxChars) {
        history = QStringLiteral("[Earlier conversation omitted]\n\n") + history.right(maxChars);
    }
    return history.trimmed();
}
}

MainWindow::MainWindow(llm_gui::core::AppSettings &settings,
                       llm_gui::core::SessionStore &sessionStore,
                       QWidget *parent)
    : QMainWindow(parent)
    , m_settings(settings)
    , m_sessionStore(sessionStore)
    , m_deepSeekClient(new llm_gui::services::DeepSeekClient(this))
    , m_llamaCppClient(new llm_gui::services::LlamaCppClient(this))
    
    , m_healthProbe(new llm_gui::services::ProviderHealthProbe(this))
    , m_activeClient(nullptr)
    , m_sessionList(new QListWidget(this))
    , m_transcript(new QTextBrowser(this))
    , m_promptEditor(new QTextEdit(this))
    , m_enableToolsCheckBox(new QCheckBox("Enable tools", this))
    , m_attachButton(new QPushButton("Attach Files", this))
    , m_clearAttachmentsButton(new QPushButton("Clear Attachments", this))
    , m_exportReplyButton(new QPushButton("Export Last Reply", this))
    , m_sendButton(new QPushButton("Send", this))
    , m_stopButton(new QPushButton("Stop", this))
    , m_newSessionButton(new QPushButton("New Chat", this))
    , m_saveSessionButton(new QPushButton("Save", this))
    , m_deleteSessionButton(new QPushButton("Delete", this))
    , m_modelCombo(new QComboBox(this))
    , m_attachmentLabel(new QLabel("No attachments", this))
    , m_statusLabel(new QLabel("Ready", this))
    , m_healthLabel(new QLabel("Health: pending", this))
    , m_terminalDock(new QDockWidget(QStringLiteral("Terminal"), this))
    , m_terminal(new PtyTerminal(this))
{
    resize(1320, 860);
    setAcceptDrops(true);

    m_terminalDock->setObjectName(QStringLiteral("terminalDock"));
    m_terminalDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::RightDockWidgetArea);
    m_terminalDock->setWidget(m_terminal);
    addDockWidget(Qt::BottomDockWidgetArea, m_terminalDock);
    m_terminalDock->hide();
    m_terminal->startShell();

    auto *central = new QWidget(this);
    auto *rootLayout = new QHBoxLayout(central);

    auto *sidebar = new QWidget(this);
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->addWidget(new QLabel("Sessions", this));
    sidebarLayout->addWidget(m_sessionList, 1);
    sidebarLayout->addWidget(m_newSessionButton);
    sidebarLayout->addWidget(m_saveSessionButton);
    sidebarLayout->addWidget(m_deleteSessionButton);

    auto *mainPane = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(mainPane);

    auto *header = new QLabel("LLM-GUI native Qt LLM client", this);
    header->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_transcript->setOpenExternalLinks(true);
    m_transcript->setPlaceholderText("Assistant transcript will appear here.");
    m_transcript->document()->setDefaultStyleSheet(
        "body { background:#0b1220; color:#e5e7eb; }"
        "div, p, span { color:#e5e7eb; }"
        "a { color:#60a5fa; }"
        "code { color:#f9fafb; }"
        "pre { background:#111827; color:#e5e7eb; border:1px solid #374151; border-radius:8px; padding:12px; }"
    );
    m_transcript->setStyleSheet(
        "QTextBrowser {"
        "  background:#0b1220;"
        "  color:#e5e7eb;"
        "  border:1px solid #374151;"
        "  border-radius:8px;"
        "}"
    );
    m_promptEditor->setPlaceholderText("Enter a prompt. Press Ctrl+Return to send. You can also drag files onto the window.");
    m_promptEditor->setMinimumHeight(140);
    m_attachmentLabel->setWordWrap(true);
    m_attachmentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_enableToolsCheckBox->setToolTip("Lets the app execute a small set of local tools when the model asks for them.");

    auto *submitShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), m_promptEditor);
    connect(submitShortcut, &QShortcut::activated, this, &MainWindow::sendPrompt);

    auto *buttonRow = new QWidget(this);
    auto *buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->addWidget(m_enableToolsCheckBox);
    buttonLayout->addWidget(m_attachButton);
    buttonLayout->addWidget(m_clearAttachmentsButton);
    buttonLayout->addWidget(m_exportReplyButton);
    buttonLayout->addWidget(m_sendButton);
    buttonLayout->addWidget(m_stopButton);
    buttonLayout->addStretch(1);

    mainLayout->addWidget(header);
    mainLayout->addWidget(m_transcript, 1);
    mainLayout->addWidget(m_promptEditor);
    mainLayout->addWidget(m_attachmentLabel);
    mainLayout->addWidget(buttonRow);

    rootLayout->addWidget(sidebar, 0);
    rootLayout->addWidget(mainPane, 1);

    setCentralWidget(central);

    auto *fileMenu = menuBar()->addMenu("File");
    auto *viewMenu = menuBar()->addMenu("View");
    auto *attachAction = fileMenu->addAction("Attach Files");
    auto *clearAttachmentsAction = fileMenu->addAction("Clear Attachments");
    auto *exportReplyAction = fileMenu->addAction("Export Last Assistant Reply");
    auto *reloadPluginsAction = fileMenu->addAction("Reload Plugins");
    auto *managePluginsAction = fileMenu->addAction("Plugin Manager");
    fileMenu->addSeparator();
    auto *settingsAction = fileMenu->addAction("Settings");
    auto *saveAction = fileMenu->addAction("Save Session");
    auto *newAction = fileMenu->addAction("New Session");
    auto *refreshHealthAction = fileMenu->addAction("Refresh Provider Health");
    auto *toggleTerminalAction = viewMenu->addAction("Terminal");
    toggleTerminalAction->setCheckable(true);

    m_modelCombo->setEditable(true);
    m_modelCombo->setMinimumContentsLength(20);
    m_modelCombo->setSizeAdjustPolicy(QComboBox::AdjustToContentsOnFirstShow);

    auto *toolbar = addToolBar("Main");
    toolbar->addAction(newAction);
    toolbar->addAction(attachAction);
    toolbar->addAction(exportReplyAction);
    toolbar->addAction(reloadPluginsAction);
    toolbar->addAction(managePluginsAction);
    toolbar->addAction(saveAction);
    toolbar->addAction(settingsAction);
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel("Model:", this));
    toolbar->addWidget(m_modelCombo);
    auto *refreshModelsAction = toolbar->addAction("Refresh Models");
    toolbar->addAction(refreshHealthAction);

    connect(attachAction, &QAction::triggered, this, &MainWindow::attachFiles);
    connect(clearAttachmentsAction, &QAction::triggered, this, &MainWindow::clearAttachments);
    connect(exportReplyAction, &QAction::triggered, this, &MainWindow::exportLastAssistantReply);
    connect(reloadPluginsAction, &QAction::triggered, this, &MainWindow::reloadPlugins);
    connect(managePluginsAction, &QAction::triggered, this, &MainWindow::managePlugins);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettings);
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveCurrentSession);
    connect(newAction, &QAction::triggered, this, &MainWindow::createNewSession);
    connect(refreshHealthAction, &QAction::triggered, this, &MainWindow::runProviderHealthChecks);
    connect(toggleTerminalAction, &QAction::toggled, m_terminalDock, &QDockWidget::setVisible);
    connect(m_terminalDock, &QDockWidget::visibilityChanged, toggleTerminalAction, &QAction::setChecked);
    connect(refreshModelsAction, &QAction::triggered, this, &MainWindow::refreshAvailableModels);

    statusBar()->addWidget(new QLabel("LLM-GUI native Qt target", this));
    statusBar()->addPermanentWidget(m_healthLabel, 1);
    statusBar()->addPermanentWidget(m_statusLabel);

    connect(m_attachButton, &QPushButton::clicked, this, &MainWindow::attachFiles);
    connect(m_clearAttachmentsButton, &QPushButton::clicked, this, &MainWindow::clearAttachments);
    connect(m_exportReplyButton, &QPushButton::clicked, this, &MainWindow::exportLastAssistantReply);
    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::sendPrompt);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::stopPrompt);
    connect(m_newSessionButton, &QPushButton::clicked, this, &MainWindow::createNewSession);
    connect(m_saveSessionButton, &QPushButton::clicked, this, &MainWindow::saveCurrentSession);
    connect(m_deleteSessionButton, &QPushButton::clicked, this, &MainWindow::deleteSelectedSession);
    connect(m_modelCombo, &QComboBox::textActivated, this, [this](const QString &) { applySelectedModel(); });
    connect(m_modelCombo->lineEdit(), &QLineEdit::editingFinished, this, &MainWindow::applySelectedModel);
    connect(m_sessionList, &QListWidget::itemActivated, this, [this](QListWidgetItem *) { loadSelectedSession(); });
    connect(m_sessionList, &QListWidget::itemClicked, this, [this](QListWidgetItem *) { loadSelectedSession(); });
    connect(m_healthProbe, &llm_gui::services::ProviderHealthProbe::healthUpdated,
            this, &MainWindow::updateProviderHealth);

    bindProvider(m_deepSeekClient);
    bindProvider(m_llamaCppClient);

    m_toolRegistry.setPluginPolicy(m_settings.trustedPlugins(), m_settings.disabledPlugins(), m_settings.allGrantedPluginScopes());
    refreshClientSettings();
    reloadSessionList();
    createNewSession();
    runProviderHealthChecks();
    refreshAvailableModels();
    refreshAttachmentStatus();
    reloadPlugins();
    setBusy(false);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData() && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    QMainWindow::dragEnterEvent(event);
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (!event->mimeData() || !event->mimeData()->hasUrls()) {
        QMainWindow::dropEvent(event);
        return;
    }

    QStringList paths;
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            paths << url.toLocalFile();
        }
    }

    addAttachmentsFromPaths(paths);
    event->acceptProposedAction();
}

void MainWindow::bindProvider(llm_gui::services::ProviderClient *client)
{
    connect(client, &llm_gui::services::ProviderClient::tokenReceived,
            this, &MainWindow::appendAssistantToken);
    connect(client, &llm_gui::services::ProviderClient::requestFailed,
            this, &MainWindow::requestFailed);
    connect(client, &llm_gui::services::ProviderClient::statusChanged,
            m_statusLabel, &QLabel::setText);
    connect(client, &llm_gui::services::ProviderClient::streamStarted, this, [this]() {
        setBusy(true);
        m_activeAssistantBuffer.clear();
        refreshTranscriptView();
    });
    connect(client, &llm_gui::services::ProviderClient::streamFinished,
            this, &MainWindow::finalizeAssistantMessage);
}

llm_gui::services::ProviderClient *MainWindow::activeProvider() const
{
    return m_activeClient;
}

void MainWindow::ensureSessionInitialized()
{
    if (!m_currentSession) {
        createNewSession();
    }
}

void MainWindow::sendPrompt()
{
    const QString prompt = currentPrompt().trimmed();
    if (prompt.isEmpty()) {
        QMessageBox::warning(this, "Empty prompt", "Enter a prompt before sending.");
        return;
    }

    ensureSessionInitialized();

    QString visiblePrompt = prompt;
    if (!m_pendingAttachments.isEmpty()) {
        visiblePrompt += "\n\nAttached files:\n";
        visiblePrompt += llm_gui::util::AttachmentProcessor::buildAttachmentSummary(m_pendingAttachments);
    }

    appendMessage("user", visiblePrompt);
    m_promptEditor->clear();

    refreshClientSettings();
    if (!activeProvider()) {
        QMessageBox::warning(this, "No provider", "No active provider is configured.");
        return;
    }

    if (m_enableToolsCheckBox->isChecked()) {
        runToolEnabledPrompt(prompt, visiblePrompt);
    } else {
        const QString providerPrompt = buildEffectivePrompt(prompt);
        activeProvider()->sendPrompt(providerPrompt, m_settings.streamEnabled());
    }

    m_pendingAttachments.clear();
    refreshAttachmentStatus();
}

void MainWindow::runToolEnabledPrompt(const QString &prompt, const QString &visiblePrompt)
{
    Q_UNUSED(visiblePrompt);

    setBusy(true);
    m_activeAssistantBuffer = "[tool-enabled request in progress]";
    refreshTranscriptView();

    QString conversation = buildToolConversation(prompt);

    QString finalResponse;
    QString error;

    for (int iteration = 0; iteration < kMaxToolIterations; ++iteration) {
        statusBar()->showMessage(QString("Tool loop iteration %1 of %2").arg(iteration + 1).arg(kMaxToolIterations), 2000);

        QString assistantText;
        if (!activeProvider()->sendPromptSync(conversation, &assistantText, &error)) {
            setBusy(false);
            m_activeAssistantBuffer.clear();
            refreshTranscriptView();
            QMessageBox::critical(this, "Provider request failed", error);
            return;
        }

        assistantText = assistantText.trimmed();
        if (assistantText.isEmpty()) {
            finalResponse = QStringLiteral("The provider returned an empty response during the tool loop.");
            break;
        }

        QString toolName;
        QJsonObject arguments;
        QString parseError;
        if (!m_toolRegistry.tryParseToolCall(assistantText, &toolName, &arguments, &parseError)) {
            finalResponse = assistantText;
            break;
        }

        const auto toolResult = m_toolRegistry.execute(toolName, arguments);
        appendMessage("assistant",
                      QStringLiteral("[Tool request] %1\n%2")
                          .arg(toolName,
                               QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Indented))));
        appendMessage("tool",
                      QStringLiteral("[%1] %2")
                          .arg(toolResult.success ? QStringLiteral("success") : QStringLiteral("error"),
                               toolResult.output));

        conversation += QStringLiteral("\n\nAssistant tool call:\n%1").arg(assistantText);
        conversation += QStringLiteral("\n\nTool result (%1):\n%2")
                            .arg(toolResult.success ? QStringLiteral("success") : QStringLiteral("error"),
                                 toolResult.output);
        conversation += QStringLiteral(
            "\n\nContinue. Emit one tool_call JSON object only if another tool is required. "
            "Otherwise answer the user normally.");
    }

    if (finalResponse.isEmpty()) {
        finalResponse = QStringLiteral("Tool loop ended without a final natural-language response.");
    }

    m_activeAssistantBuffer = finalResponse;
    finalizeAssistantMessage();
}

void MainWindow::stopPrompt()
{
    if (activeProvider()) {
        activeProvider()->cancel();
    }
    setBusy(false);
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(m_settings, this);
    if (dialog.exec() == QDialog::Accepted) {
        refreshClientSettings();
        runProviderHealthChecks();
        refreshAvailableModels();
        updateWindowTitle();
    }
}

void MainWindow::attachFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        "Attach Files",
        QString(),
        "Supported files (*.txt *.md *.markdown *.rst *.json *.yaml *.yml *.xml *.csv *.log *.ini *.cfg *.conf *.toml *.c *.cc *.cpp *.cxx *.h *.hpp *.hh *.py *.js *.ts *.tsx *.jsx *.java *.kt *.rs *.go *.rb *.php *.swift *.cs *.sh *.bash *.zsh *.ps1 *.sql *.html *.css *.docx *.zip);;All files (*)");

    addAttachmentsFromPaths(files);
}

void MainWindow::addAttachmentsFromPaths(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return;
    }

    const auto attachments = llm_gui::util::AttachmentProcessor::processFiles(paths);
    for (const auto &attachment : attachments) {
        m_pendingAttachments.push_back(attachment);
    }
    refreshAttachmentStatus();
}

void MainWindow::clearAttachments()
{
    m_pendingAttachments.clear();
    refreshAttachmentStatus();
}

void MainWindow::exportLastAssistantReply()
{
    const QString content = lastAssistantMessage();
    if (content.trimmed().isEmpty()) {
        QMessageBox::information(this, "No assistant reply", "There is no assistant reply to export yet.");
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this,
        "Export Last Assistant Reply",
        QDir::home().filePath("assistant-reply.md"),
        "Markdown (*.md);;Text (*.txt);;All files (*)");

    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, "Export failed", QString("Unable to write file: %1").arg(path));
        return;
    }

    file.write(content.toUtf8());
    file.close();
    statusBar()->showMessage(QString("Exported assistant reply to %1").arg(path), 5000);
}

void MainWindow::reloadPlugins()
{
    m_toolRegistry.setPluginPolicy(m_settings.trustedPlugins(), m_settings.disabledPlugins(), m_settings.allGrantedPluginScopes());
    const QString message = m_toolRegistry.reloadPlugins();
    showPluginReloadStatus(message);
}

void MainWindow::showPluginReloadStatus(const QString &message)
{
    statusBar()->showMessage(message.section('\n', 0, 0), 8000);
    QMessageBox::information(this, "Plugins", message);
}

void MainWindow::managePlugins()
{
    PluginManagerDialog dialog(m_settings, m_toolRegistry, this);
    dialog.exec();
}

void MainWindow::appendAssistantToken(const QString &token)
{
    m_activeAssistantBuffer += token;
    refreshTranscriptView();
}

void MainWindow::finalizeAssistantMessage()
{
    if (!m_busy && m_activeAssistantBuffer.isEmpty()) {
        return;
    }

    setBusy(false);
    if (!m_activeAssistantBuffer.isEmpty()) {
        appendMessage("assistant", m_activeAssistantBuffer);
        m_activeAssistantBuffer.clear();
        saveCurrentSession();
    }
    refreshTranscriptView();
}

void MainWindow::requestFailed(const QString &message)
{
    setBusy(false);
    QMessageBox::critical(this, "Provider request failed", message);
}

void MainWindow::refreshClientSettings()
{
    autoConfigureLlamaCppModel();
    const bool llamaCppAvailable = llm_gui::services::LlamaCppClient::isEmbeddedBackendAvailable();

    m_deepSeekClient->setBaseUrl(m_settings.deepSeekBaseUrl());
    m_deepSeekClient->setModel(m_settings.deepSeekModel());
    m_deepSeekClient->setApiKey(m_settings.deepSeekApiKey());
    m_deepSeekClient->setRequestTimeoutMs(m_settings.requestTimeoutMs());

    m_llamaCppClient->setModelPath(m_settings.llamaCppModelPath());
    m_llamaCppClient->setModel(m_settings.llamaCppModelLabel());
    m_llamaCppClient->setContextSize(m_settings.llamaCppContextSize());
    m_llamaCppClient->setGpuLayers(m_settings.llamaCppGpuLayers());
    m_llamaCppClient->setExtraArgs(m_settings.llamaCppExtraArgs());
    m_llamaCppClient->setRequestTimeoutMs(m_settings.requestTimeoutMs());

    m_sessionStore.setBasePath(m_settings.sessionStoragePath());

    QString provider = m_settings.provider();
    if (provider == QStringLiteral("llama_cpp") && !llamaCppAvailable) {
        provider = QStringLiteral("chatgpt");
        m_settings.setProvider(provider);
        m_llamaCppHealth = QStringLiteral("llama.cpp=Unavailable in this build");
        statusBar()->showMessage(llamaBackendUnavailableMessage(), 6000);
    }

    if (provider == QStringLiteral("llama_cpp")) {
        m_activeClient = m_llamaCppClient;
    } else {
        m_activeClient = m_deepSeekClient;
    }

    if (m_currentSession && m_activeClient) {
        m_currentSession->provider = m_activeClient->providerId();
        m_currentSession->model = m_activeClient->model();
    }

    if (provider == QStringLiteral("llama_cpp")) {
        populateLlamaCppModelSelector();
    } else if (m_activeClient) {
        populateModelSelector(QStringList{m_activeClient->model()}, m_activeClient->model());
    }
    updateWindowTitle();
}

void MainWindow::runProviderHealthChecks()
{
    m_healthLabel->setText(QStringLiteral("Health: probing..."));
    m_healthProbe->probeDeepSeek(m_settings.deepSeekBaseUrl(), m_settings.deepSeekApiKey());
    if (llm_gui::services::LlamaCppClient::isEmbeddedBackendAvailable()) {
        m_healthProbe->probeLlamaCpp(m_settings.llamaCppModelPath());
    } else {
        m_llamaCppHealth = QStringLiteral("llama.cpp=Unavailable in this build");
        updateProviderHealth(llm_gui::services::ProviderHealth{
            QStringLiteral("llama_cpp"),
            false,
            QStringLiteral("Unavailable in this build")
        });
    }
}

void MainWindow::updateProviderHealth(const llm_gui::services::ProviderHealth &health)
{
    if (health.providerId == QStringLiteral("chatgpt")) {
        m_deepSeekHealth = QStringLiteral("ChatGPT=%1").arg(health.detail);
    } else if (health.providerId == QStringLiteral("llama_cpp")) {
        m_llamaCppHealth = QStringLiteral("llama.cpp=%1").arg(health.detail);
    }

    QStringList parts;
    if (!m_deepSeekHealth.isEmpty()) {
        parts << m_deepSeekHealth;
    }
    if (!m_llamaCppHealth.isEmpty()) {
        parts << m_llamaCppHealth;
    }
    if (parts.isEmpty()) {
        parts << QStringLiteral("Health: pending");
    }

    m_healthLabel->setText(parts.join(QStringLiteral(" | ")));
}


void MainWindow::populateModelSelector(const QStringList &models, const QString &selectedModel)
{
    QSignalBlocker blocker(m_modelCombo);
    m_modelCombo->clear();

    QStringList entries = models;
    if (!selectedModel.trimmed().isEmpty() && !entries.contains(selectedModel)) {
        entries.prepend(selectedModel);
    }

    entries.removeDuplicates();
    m_modelCombo->addItems(entries);

    const int index = m_modelCombo->findText(selectedModel);
    if (index >= 0) {
        m_modelCombo->setCurrentIndex(index);
    } else {
        m_modelCombo->setEditText(selectedModel);
    }
}

void MainWindow::autoConfigureLlamaCppModel()
{
    const QString resolvedPath = m_settings.llamaCppModelPath().trimmed();
    const QFileInfo resolvedInfo(resolvedPath);
    if (!resolvedInfo.exists() || !resolvedInfo.isFile() || !resolvedInfo.isReadable()) {
        return;
    }

    const QString normalizedPath = resolvedInfo.canonicalFilePath().isEmpty()
        ? resolvedInfo.absoluteFilePath()
        : resolvedInfo.canonicalFilePath();
    const QString configuredPath = m_settings.configuredLlamaCppModelPath().trimmed();
    const QString configuredNormalized = QFileInfo(configuredPath).exists()
        ? (QFileInfo(configuredPath).canonicalFilePath().isEmpty()
               ? QFileInfo(configuredPath).absoluteFilePath()
               : QFileInfo(configuredPath).canonicalFilePath())
        : QString();

    if (configuredNormalized != normalizedPath) {
        m_settings.setLlamaCppModelPath(normalizedPath);
    }

    const QString autoLabel = llamaModelLabelForPath(normalizedPath);
    const QString configuredLabel = m_settings.llamaCppModelLabel().trimmed();
    if ((configuredLabel.isEmpty() || configuredLabel == QStringLiteral("local-gguf")) && !autoLabel.isEmpty()) {
        m_settings.setLlamaCppModelLabel(autoLabel);
    }
}

void MainWindow::populateLlamaCppModelSelector()
{
    QSignalBlocker blocker(m_modelCombo);
    m_modelCombo->clear();

    QStringList modelPaths = m_settings.discoverLlamaCppModelPaths();
    const QString selectedPath = m_settings.llamaCppModelPath().trimmed();
    const QString selectedLabel = m_settings.llamaCppModelLabel().trimmed();

    const QString normalizedSelectedPath = QFileInfo(selectedPath).exists()
        ? (QFileInfo(selectedPath).canonicalFilePath().isEmpty()
               ? QFileInfo(selectedPath).absoluteFilePath()
               : QFileInfo(selectedPath).canonicalFilePath())
        : selectedPath;

    if (!normalizedSelectedPath.isEmpty() && !modelPaths.contains(normalizedSelectedPath)) {
        modelPaths.prepend(normalizedSelectedPath);
    }

    QHash<QString, int> labelCounts;
    for (const QString &modelPath : modelPaths) {
        const QString label = llamaModelLabelForPath(modelPath);
        if (!label.isEmpty()) {
            labelCounts[label] += 1;
        }
    }

    int selectedIndex = -1;
    for (const QString &modelPath : modelPaths) {
        const QString normalizedPath = QFileInfo(modelPath).exists()
            ? (QFileInfo(modelPath).canonicalFilePath().isEmpty()
                   ? QFileInfo(modelPath).absoluteFilePath()
                   : QFileInfo(modelPath).canonicalFilePath())
            : modelPath;
        const QString storedLabel = (normalizedPath == normalizedSelectedPath && !selectedLabel.isEmpty())
            ? selectedLabel
            : llamaModelLabelForPath(normalizedPath);
        const QString displayLabel = (normalizedPath == normalizedSelectedPath && !selectedLabel.isEmpty())
            ? selectedLabel
            : llamaDisplayLabelForPath(normalizedPath, labelCounts);

        m_modelCombo->addItem(displayLabel, normalizedPath);
        const int index = m_modelCombo->count() - 1;
        m_modelCombo->setItemData(index, storedLabel, Qt::UserRole + 1);

        if (normalizedPath == normalizedSelectedPath) {
            selectedIndex = index;
        }
    }

    if (selectedIndex >= 0) {
        m_modelCombo->setCurrentIndex(selectedIndex);
    } else {
        const QString fallbackLabel = selectedLabel.isEmpty() ? QStringLiteral("local-gguf") : selectedLabel;
        m_modelCombo->setEditText(fallbackLabel);
    }
}

void MainWindow::refreshAvailableModels()
{
    if (m_settings.provider() == QStringLiteral("llama_cpp")
        && llm_gui::services::LlamaCppClient::isEmbeddedBackendAvailable()) {
        autoConfigureLlamaCppModel();
        const QStringList discoveredPaths = m_settings.discoverLlamaCppModelPaths();
        populateLlamaCppModelSelector();

        if (discoveredPaths.isEmpty()) {
            m_modelCombo->setToolTip(QStringLiteral("No GGUF models were auto-detected. Open Settings to browse for a file manually."));
            runProviderHealthChecks();
            statusBar()->showMessage(QStringLiteral("No GGUF models were auto-detected. Open Settings to choose one manually."), 6000);
            return;
        }

        m_modelCombo->setToolTip(QStringLiteral("Choose from auto-detected GGUF models. Selecting one updates the active llama.cpp model path automatically."));
        runProviderHealthChecks();
        statusBar()->showMessage(QStringLiteral("Auto-detected %1 GGUF model(s) for llama.cpp.").arg(discoveredPaths.size()), 4000);
        return;
    }

    QString error;
    const QStringList models = m_deepSeekClient->fetchAvailableModelsSync(&error);
    if (models.isEmpty()) {
        populateModelSelector(QStringList{m_settings.deepSeekModel()}, m_settings.deepSeekModel());
        m_modelCombo->setToolTip(QStringLiteral("Set the active OpenAI model here, or use Settings to change the base URL and API key."));
        if (!error.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Unable to refresh OpenAI models: %1").arg(error), 6000);
        }
        return;
    }

    populateModelSelector(models, m_settings.deepSeekModel());
    m_modelCombo->setToolTip(QStringLiteral("Choose from OpenAI models returned by /models."));
    statusBar()->showMessage(QStringLiteral("Loaded %1 OpenAI models.").arg(models.size()), 4000);
}

void MainWindow::applySelectedModel()
{
    const QString modelName = m_modelCombo->currentText().trimmed();
    if (modelName.isEmpty()) {
        return;
    }

    if (m_settings.provider() == QStringLiteral("llama_cpp")
        && llm_gui::services::LlamaCppClient::isEmbeddedBackendAvailable()) {
        const QString selectedPath = m_modelCombo->currentData().toString().trimmed();
        const QString selectedLabel = m_modelCombo->currentData(Qt::UserRole + 1).toString().trimmed();

        if (!selectedPath.isEmpty()) {
            const QString resolvedLabel = selectedLabel.isEmpty() ? llamaModelLabelForPath(selectedPath) : selectedLabel;
            m_settings.setLlamaCppModelPath(selectedPath);
            m_settings.setLlamaCppModelLabel(resolvedLabel.isEmpty() ? modelName : resolvedLabel);
            m_llamaCppClient->setModelPath(selectedPath);
            m_llamaCppClient->setModel(resolvedLabel.isEmpty() ? modelName : resolvedLabel);
            populateLlamaCppModelSelector();
        } else {
            m_settings.setLlamaCppModelLabel(modelName);
            m_llamaCppClient->setModel(modelName);
        }
    } else {
        m_settings.setDeepSeekModel(modelName);
        m_deepSeekClient->setModel(modelName);
    }

    if (m_currentSession && activeProvider()) {
        m_currentSession->provider = activeProvider()->providerId();
        m_currentSession->model = activeProvider()->model();
    }

    runProviderHealthChecks();
    updateWindowTitle();
    statusBar()->showMessage(QStringLiteral("Model set to %1").arg(modelName), 3000);
}

void MainWindow::createNewSession()
{
    const QString provider = m_settings.provider();
    const QString model = (provider == QStringLiteral("llama_cpp"))
        ? m_settings.llamaCppModelLabel()
        : m_settings.deepSeekModel();

    m_currentSession = std::make_unique<llm_gui::core::ChatSession>(
        llm_gui::core::ChatSession::createEmpty(provider, model));

    m_activeAssistantBuffer.clear();
    m_pendingAttachments.clear();
    refreshTranscriptView();
    refreshAttachmentStatus();
    if (m_settings.provider() == QStringLiteral("llama_cpp")) {
        populateLlamaCppModelSelector();
    } else {
        populateModelSelector(QStringList{m_activeClient->model()}, m_activeClient->model());
    }
    updateWindowTitle();
}

void MainWindow::saveCurrentSession()
{
    ensureSessionInitialized();

    QString errorMessage;
    if (!m_sessionStore.saveSession(*m_currentSession, &errorMessage)) {
        QMessageBox::warning(this, "Save failed", errorMessage);
        return;
    }

    reloadSessionList();
    if (m_settings.provider() == QStringLiteral("llama_cpp")) {
        populateLlamaCppModelSelector();
    } else {
        populateModelSelector(QStringList{m_activeClient->model()}, m_activeClient->model());
    }
    updateWindowTitle();
}

void MainWindow::loadSelectedSession()
{
    QListWidgetItem *item = m_sessionList->currentItem();
    if (!item) {
        return;
    }

    const QString sessionId = item->data(Qt::UserRole).toString();
    llm_gui::core::ChatSession session;
    QString errorMessage;

    if (!m_sessionStore.loadSession(sessionId, &session, &errorMessage)) {
        QMessageBox::warning(this, "Load failed", errorMessage);
        return;
    }

    m_currentSession = std::make_unique<llm_gui::core::ChatSession>(session);
    m_activeAssistantBuffer.clear();
    m_pendingAttachments.clear();
    refreshTranscriptView();
    refreshAttachmentStatus();
    if (m_settings.provider() == QStringLiteral("llama_cpp")) {
        populateLlamaCppModelSelector();
    } else {
        populateModelSelector(QStringList{m_activeClient->model()}, m_activeClient->model());
    }
    updateWindowTitle();
}

void MainWindow::deleteSelectedSession()
{
    QListWidgetItem *item = m_sessionList->currentItem();
    if (!item) {
        return;
    }

    const QString sessionId = item->data(Qt::UserRole).toString();
    const auto response = QMessageBox::question(
        this,
        "Delete session",
        "Delete the selected session file from local storage?");

    if (response != QMessageBox::Yes) {
        return;
    }

    QString errorMessage;
    if (!m_sessionStore.deleteSession(sessionId, &errorMessage)) {
        QMessageBox::warning(this, "Delete failed", errorMessage);
        return;
    }

    if (m_currentSession && m_currentSession->id == sessionId) {
        m_currentSession.reset();
        createNewSession();
    }

    reloadSessionList();
}

void MainWindow::appendMessage(const QString &role, const QString &content)
{
    ensureSessionInitialized();

    llm_gui::core::ChatMessage message;
    message.role = role;
    message.content = content;
    message.timestamp = QDateTime::currentDateTimeUtc();

    m_currentSession->messages.push_back(message);
    m_currentSession->title = m_currentSession->suggestedTitle();
    m_currentSession->updatedAt = message.timestamp;

    refreshTranscriptView();
    populateModelSelector(QStringList{m_activeClient->model()}, m_activeClient->model());
    updateWindowTitle();
}

QString MainWindow::currentPrompt() const
{
    return m_promptEditor->toPlainText();
}

QString MainWindow::promptWithAttachments(const QString &prompt) const
{
    return llm_gui::util::AttachmentProcessor::buildPromptEnvelope(prompt, m_pendingAttachments);
}

QString MainWindow::buildEffectivePrompt(const QString &prompt) const
{
    const QString userPrompt = promptWithAttachments(prompt);
    const QString customInstructions = m_settings.customInstructions().trimmed();
    const QString history = formatRecentMessagesForPrompt(m_currentSession, kMaxPromptHistoryMessages, kMaxPromptHistoryChars, 1);

    QStringList parts;
    if (!customInstructions.isEmpty()) {
        parts << QStringLiteral("Custom instructions:\n%1").arg(customInstructions);
    }
    if (!history.isEmpty()) {
        parts << QStringLiteral("Recent conversation:\n%1").arg(history);
    }
    parts << QStringLiteral("User request:\n%1").arg(userPrompt);
    return parts.join(QStringLiteral("\n\n"));
}

QString MainWindow::buildToolConversation(const QString &prompt) const
{
    const QString customInstructions = m_settings.customInstructions().trimmed();
    const QString history = formatRecentMessagesForPrompt(m_currentSession, kMaxPromptHistoryMessages, kMaxPromptHistoryChars, 1);

    QStringList parts;
    parts << m_toolRegistry.usageInstructions();
    parts << QStringLiteral("Decide whether a tool is actually needed. Do not call a tool when you can answer directly.");
    parts << QStringLiteral("When calling a tool, output ONLY the JSON tool_call object with no markdown, prose, or code fences.");
    parts << QStringLiteral("When you already have enough information, answer the user normally in plain text.");

    if (!customInstructions.isEmpty()) {
        parts << QStringLiteral("Persistent custom instructions:\n%1").arg(customInstructions);
    }
    if (!history.isEmpty()) {
        parts << QStringLiteral("Recent conversation:\n%1").arg(history);
    }
    parts << QStringLiteral("Current user request:\n%1").arg(promptWithAttachments(prompt));
    return parts.join(QStringLiteral("\n\n"));
}


QString MainWindow::lastAssistantMessage() const
{
    if (!m_currentSession) {
        return {};
    }

    for (auto it = m_currentSession->messages.crbegin(); it != m_currentSession->messages.crend(); ++it) {
        if (it->role == "assistant") {
            return it->content;
        }
    }
    return {};
}

void MainWindow::refreshAttachmentStatus()
{
    if (m_pendingAttachments.isEmpty()) {
        m_attachmentLabel->setText("No attachments");
        return;
    }

    QStringList lines;
    for (const auto &attachment : m_pendingAttachments) {
        QString line = QString("• %1 (%2)").arg(attachment.displayName, attachment.typeLabel);
        if (!attachment.warning.isEmpty()) {
            line += QString(" — %1").arg(attachment.warning);
        } else if (attachment.includedInPrompt) {
            line += " — ready";
        }
        lines << line;
    }

    m_attachmentLabel->setText(QString("Attachments:\n%1").arg(lines.join('\n')));
}

void MainWindow::setBusy(bool busy)
{
    m_busy = busy;
    m_enableToolsCheckBox->setEnabled(!busy);
    m_attachButton->setEnabled(!busy);
    m_clearAttachmentsButton->setEnabled(!busy);
    m_exportReplyButton->setEnabled(!busy);
    m_sendButton->setEnabled(!busy);
    m_stopButton->setEnabled(busy);
}

void MainWindow::refreshTranscriptView()
{
    if (!m_currentSession) {
        m_transcript->setHtml("<html><body><p>No active session.</p></body></html>");
        return;
    }

    m_transcript->setHtml(llm_gui::util::TranscriptFormatter::formatMessages(
        m_currentSession->messages, m_activeAssistantBuffer));
    m_transcript->moveCursor(QTextCursor::End);
}

void MainWindow::reloadSessionList()
{
    m_sessionList->clear();
    const auto sessions = m_sessionStore.listSessions();

    for (const llm_gui::core::ChatSession &session : sessions) {
        auto *item = new QListWidgetItem(session.suggestedTitle(), m_sessionList);
        item->setData(Qt::UserRole, session.id);
        item->setToolTip(QString("%1\n%2").arg(session.provider, session.model));
    }
}

void MainWindow::updateWindowTitle()
{
    QString providerName = m_activeClient ? m_activeClient->displayName() : QStringLiteral("No Provider");
    QString title = QString("LLM-GUI - %1").arg(providerName);

    if (m_currentSession) {
        title += QString(" - %1").arg(m_currentSession->title);
    }

    setWindowTitle(title);
}

} // namespace llm_gui::app
