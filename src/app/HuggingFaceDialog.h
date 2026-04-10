#pragma once

#include <QDialog>
#include <QVector>

#include "services/HuggingFaceHubClient.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace llm_gui::app {

class HuggingFaceDialog : public QDialog
{
    Q_OBJECT
public:
    explicit HuggingFaceDialog(const QString &token,
                               const QString &downloadDirectory,
                               int requestTimeoutMs,
                               QWidget *parent = nullptr);

    QString downloadedModelPath() const;
    QString token() const;

private slots:
    void browseDownloadDirectory();
    void runSearch();
    void handleSelectedModelChanged();
    void downloadSelectedFile();

private:
    void updateSelectedFiles(const llm_gui::services::HuggingFaceModel &model);
    llm_gui::services::HuggingFaceModel currentModel() const;

    QLineEdit *m_searchEdit;
    QPushButton *m_searchButton;
    QLineEdit *m_tokenEdit;
    QListWidget *m_resultsList;
    QComboBox *m_filesCombo;
    QLineEdit *m_downloadDirEdit;
    QPushButton *m_browseDownloadDirButton;
    QLabel *m_statusLabel;
    QPushButton *m_downloadButton;

    llm_gui::services::HuggingFaceHubClient m_client;
    QVector<llm_gui::services::HuggingFaceModel> m_results;
    QString m_downloadedModelPath;
};

} // namespace llm_gui::app
