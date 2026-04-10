#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTextEdit;

namespace llm_gui::core { class AppSettings; }

namespace llm_gui::app {

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(llm_gui::core::AppSettings &settings, QWidget *parent = nullptr);

private slots:
    void saveAndAccept();
    void refreshDeepSeekModels();

private:
    void updateProviderAvailability();

    llm_gui::core::AppSettings &m_settings;
    QComboBox *m_providerCombo;
    QLineEdit *m_deepSeekBaseUrlEdit;
    QComboBox *m_deepSeekModelCombo;
    QPushButton *m_refreshDeepSeekModelsButton;
    QLineEdit *m_deepSeekApiKeyEdit;
    QLineEdit *m_llamaCppModelPathEdit;
    QPushButton *m_browseLlamaCppModelPathButton;
    QLineEdit *m_llamaCppModelLabelEdit;
    QLineEdit *m_sessionPathEdit;
    QTextEdit *m_customInstructionsEdit;
    QCheckBox *m_streamCheck;
    QSpinBox *m_llamaCppContextSpin;
    QSpinBox *m_llamaCppGpuLayersSpin;
    QPlainTextEdit *m_llamaCppExtraArgsEdit;
    QSpinBox *m_timeoutSpin;
    bool m_llamaCppAvailable { false };
};

} // namespace llm_gui::app
