#include "app/SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>

#include "core/AppSettings.h"
#include "services/DeepSeekClient.h"
#include "services/LlamaCppClient.h"

namespace llm_gui::app {

SettingsDialog::SettingsDialog(llm_gui::core::AppSettings &settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_providerCombo(new QComboBox(this))
    , m_deepSeekBaseUrlEdit(new QLineEdit(this))
    , m_deepSeekModelCombo(new QComboBox(this))
    , m_refreshDeepSeekModelsButton(new QPushButton("Refresh OpenAI Models", this))
    , m_deepSeekApiKeyEdit(new QLineEdit(this))
    , m_llamaCppModelPathEdit(new QLineEdit(this))
    , m_browseLlamaCppModelPathButton(new QPushButton("Browse...", this))
    , m_llamaCppModelLabelEdit(new QLineEdit(this))
    , m_sessionPathEdit(new QLineEdit(this))
    , m_customInstructionsEdit(new QTextEdit(this))
    , m_streamCheck(new QCheckBox("Enable streamed responses where supported", this))
    , m_llamaCppContextSpin(new QSpinBox(this))
    , m_llamaCppGpuLayersSpin(new QSpinBox(this))
    , m_llamaCppExtraArgsEdit(new QPlainTextEdit(this))
    , m_timeoutSpin(new QSpinBox(this)) {
    setWindowTitle(QStringLiteral("Settings"));
    resize(860, 680);

    m_llamaCppAvailable = llm_gui::services::LlamaCppClient::isEmbeddedBackendAvailable();
    m_providerCombo->addItem(QStringLiteral("ChatGPT / OpenAI"), QStringLiteral("chatgpt"));
    if (m_llamaCppAvailable) {
        m_providerCombo->addItem(QStringLiteral("llama.cpp (integrated)"), QStringLiteral("llama_cpp"));
    }

    const int providerIndex = m_providerCombo->findData(m_settings.provider());
    if (providerIndex >= 0) {
        m_providerCombo->setCurrentIndex(providerIndex);
    } else {
        m_providerCombo->setCurrentIndex(m_providerCombo->findData(QStringLiteral("chatgpt")));
    }

    m_deepSeekBaseUrlEdit->setText(m_settings.deepSeekBaseUrl());
    m_deepSeekModelCombo->setEditable(true);
    m_deepSeekModelCombo->setInsertPolicy(QComboBox::NoInsert);
    m_deepSeekModelCombo->addItem(m_settings.deepSeekModel());
    m_deepSeekModelCombo->setCurrentText(m_settings.deepSeekModel());
    m_deepSeekApiKeyEdit->setText(m_settings.deepSeekApiKey());
    m_deepSeekApiKeyEdit->setEchoMode(QLineEdit::PasswordEchoOnEdit);

    m_llamaCppModelPathEdit->setText(m_settings.llamaCppModelPath());
    m_llamaCppModelPathEdit->setPlaceholderText(QStringLiteral("Auto-detected from common model folders when possible"));
    m_llamaCppModelLabelEdit->setText(m_settings.llamaCppModelLabel());

    m_sessionPathEdit->setText(m_settings.sessionStoragePath());
    m_customInstructionsEdit->setAcceptRichText(false);
    m_customInstructionsEdit->setMinimumHeight(140);
    m_customInstructionsEdit->setPlainText(m_settings.customInstructions());

    m_streamCheck->setChecked(m_settings.streamEnabled());

    m_llamaCppContextSpin->setRange(256, 1048576);
    m_llamaCppContextSpin->setSingleStep(256);
    m_llamaCppContextSpin->setValue(m_settings.llamaCppContextSize());
    m_llamaCppGpuLayersSpin->setRange(-1, 1000);
    m_llamaCppGpuLayersSpin->setValue(m_settings.llamaCppGpuLayers());
    m_llamaCppExtraArgsEdit->setPlaceholderText(QStringLiteral("--n-predict 512 --threads 8"));
    m_llamaCppExtraArgsEdit->setPlainText(m_settings.llamaCppExtraArgs());
    m_llamaCppExtraArgsEdit->setMaximumHeight(84);

    m_timeoutSpin->setRange(1000, 3600000);
    m_timeoutSpin->setSingleStep(1000);
    m_timeoutSpin->setSuffix(QStringLiteral(" ms"));
    m_timeoutSpin->setValue(m_settings.requestTimeoutMs());

    auto *llamaModelRow = new QWidget(this);
    auto *llamaModelLayout = new QHBoxLayout(llamaModelRow);
    llamaModelLayout->setContentsMargins(0, 0, 0, 0);
    llamaModelLayout->addWidget(m_llamaCppModelPathEdit, 1);
    llamaModelLayout->addWidget(m_browseLlamaCppModelPathButton);

    auto *deepSeekModelRow = new QWidget(this);
    auto *deepSeekModelLayout = new QHBoxLayout(deepSeekModelRow);
    deepSeekModelLayout->setContentsMargins(0, 0, 0, 0);
    deepSeekModelLayout->addWidget(m_deepSeekModelCombo, 1);
    deepSeekModelLayout->addWidget(m_refreshDeepSeekModelsButton);

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Default Provider"), m_providerCombo);
    form->addRow(QStringLiteral("OpenAI Base URL"), m_deepSeekBaseUrlEdit);
    form->addRow(QStringLiteral("OpenAI Model"), deepSeekModelRow);
    form->addRow(QStringLiteral("OpenAI API Key"), m_deepSeekApiKeyEdit);
    form->addRow(QStringLiteral("llama.cpp GGUF Model Path"), llamaModelRow);
    form->addRow(QStringLiteral("llama.cpp Model Label"), m_llamaCppModelLabelEdit);
    form->addRow(QStringLiteral("llama.cpp Context Size"), m_llamaCppContextSpin);
    form->addRow(QStringLiteral("llama.cpp GPU Layers"), m_llamaCppGpuLayersSpin);
    form->addRow(QStringLiteral("llama.cpp Extra Args"), m_llamaCppExtraArgsEdit);
    form->addRow(QStringLiteral("Session Storage Path"), m_sessionPathEdit);
    form->addRow(QStringLiteral("Custom Instructions"), m_customInstructionsEdit);
    form->addRow(QString(), m_streamCheck);
    form->addRow(QStringLiteral("Request Timeout"), m_timeoutSpin);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::saveAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    connect(m_refreshDeepSeekModelsButton, &QPushButton::clicked, this, &SettingsDialog::refreshDeepSeekModels);
    connect(m_browseLlamaCppModelPathButton, &QPushButton::clicked, this, [this]() {
        const QString current = m_llamaCppModelPathEdit->text().trimmed();
        const QString selected = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("Select GGUF model"),
            current.isEmpty() ? QString() : QFileInfo(current).absolutePath(),
            QStringLiteral("GGUF Models (*.gguf *.GGUF);;All files (*)"));
        if (!selected.isEmpty()) {
            m_llamaCppModelPathEdit->setText(selected);
            if (m_llamaCppModelLabelEdit->text().trimmed().isEmpty()) {
                m_llamaCppModelLabelEdit->setText(QFileInfo(selected).completeBaseName());
            }
        }
    });

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form, 1);
    layout->addWidget(buttons);

    const auto refreshProviderFields = [this]() {
        const QString provider = m_providerCombo->currentData().toString();
        const bool isDeepSeek = (provider == QStringLiteral("chatgpt"));
        const bool isLlamaCpp = m_llamaCppAvailable && (provider == QStringLiteral("llama_cpp"));

        m_deepSeekBaseUrlEdit->setEnabled(isDeepSeek);
        m_deepSeekModelCombo->setEnabled(isDeepSeek);
        m_refreshDeepSeekModelsButton->setEnabled(isDeepSeek);
        m_deepSeekApiKeyEdit->setEnabled(isDeepSeek);

        m_llamaCppModelPathEdit->setEnabled(isLlamaCpp);
        m_browseLlamaCppModelPathButton->setEnabled(isLlamaCpp);
        m_llamaCppModelLabelEdit->setEnabled(isLlamaCpp);
        m_llamaCppContextSpin->setEnabled(isLlamaCpp);
        m_llamaCppGpuLayersSpin->setEnabled(isLlamaCpp);
        m_llamaCppExtraArgsEdit->setEnabled(isLlamaCpp);
    };
    connect(m_providerCombo, &QComboBox::currentIndexChanged, this, [refreshProviderFields](int) { refreshProviderFields(); });
    updateProviderAvailability();
    refreshProviderFields();
}

void SettingsDialog::updateProviderAvailability() {
    if (m_llamaCppAvailable) {
        return;
    }

    m_llamaCppModelPathEdit->setPlaceholderText(QStringLiteral("Embedded llama.cpp support is not available in this build"));
    m_llamaCppModelPathEdit->setToolTip(QStringLiteral("This build was compiled without embedded llama.cpp support."));
    m_llamaCppModelLabelEdit->setToolTip(QStringLiteral("This build was compiled without embedded llama.cpp support."));
    m_browseLlamaCppModelPathButton->setToolTip(QStringLiteral("This build was compiled without embedded llama.cpp support."));
}

void SettingsDialog::refreshDeepSeekModels() {
    llm_gui::services::DeepSeekClient client(this);
    client.setBaseUrl(m_deepSeekBaseUrlEdit->text());
    client.setApiKey(m_deepSeekApiKeyEdit->text());
    client.setRequestTimeoutMs(m_timeoutSpin->value());

    QString errorMessage;
    const QStringList models = client.fetchAvailableModelsSync(&errorMessage);
    if (models.isEmpty()) {
        QMessageBox::warning(this,
                             QStringLiteral("Unable to load models"),
                             errorMessage.isEmpty() ? QStringLiteral("No models were returned.") : errorMessage);
        return;
    }

    QSignalBlocker blocker(m_deepSeekModelCombo);
    const QString selected = m_deepSeekModelCombo->currentText().trimmed();
    m_deepSeekModelCombo->clear();
    m_deepSeekModelCombo->addItems(models);
    const int index = m_deepSeekModelCombo->findText(selected);
    if (index >= 0) {
        m_deepSeekModelCombo->setCurrentIndex(index);
    } else {
        m_deepSeekModelCombo->setCurrentText(selected.isEmpty() ? models.front() : selected);
    }
}

void SettingsDialog::saveAndAccept() {
    const QString selectedProvider = m_providerCombo->currentData().toString();
    const QString provider = (!m_llamaCppAvailable && selectedProvider == QStringLiteral("llama_cpp"))
        ? QStringLiteral("chatgpt")
        : selectedProvider;
    m_settings.setProvider(provider);
    m_settings.setDeepSeekBaseUrl(m_deepSeekBaseUrlEdit->text());
    m_settings.setDeepSeekModel(m_deepSeekModelCombo->currentText());
    m_settings.setDeepSeekApiKey(m_deepSeekApiKeyEdit->text());
    m_settings.setLlamaCppModelPath(m_llamaCppModelPathEdit->text());
    m_settings.setLlamaCppModelLabel(m_llamaCppModelLabelEdit->text());
    m_settings.setLlamaCppContextSize(m_llamaCppContextSpin->value());
    m_settings.setLlamaCppGpuLayers(m_llamaCppGpuLayersSpin->value());
    m_settings.setLlamaCppExtraArgs(m_llamaCppExtraArgsEdit->toPlainText());
    m_settings.setSessionStoragePath(m_sessionPathEdit->text());
    m_settings.setCustomInstructions(m_customInstructionsEdit->toPlainText());
    m_settings.setStreamEnabled(m_streamCheck->isChecked());
    m_settings.setRequestTimeoutMs(m_timeoutSpin->value());
    accept();
}

} // namespace llm_gui::app
