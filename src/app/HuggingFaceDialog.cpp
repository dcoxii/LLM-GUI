#include "app/HuggingFaceDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QAbstractItemView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace llm_gui::app {

namespace {

QString formatFileSize(qint64 sizeBytes)
{
    if (sizeBytes <= 0) {
        return QStringLiteral("size unknown");
    }

    static const char *suffixes[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(sizeBytes);
    int suffixIndex = 0;
    while (value >= 1024.0 && suffixIndex < 4) {
        value /= 1024.0;
        ++suffixIndex;
    }

    return QStringLiteral("%1 %2").arg(QString::number(value, value >= 10.0 ? 'f' : 'f', value >= 10.0 ? 1 : 2),
                                       QString::fromLatin1(suffixes[suffixIndex]));
}

QString summarizeModel(const llm_gui::services::HuggingFaceModel &model)
{
    QStringList parts;
    if (model.downloads > 0) {
        parts << QStringLiteral("%1 downloads").arg(QString::number(model.downloads));
    }
    if (model.likes > 0) {
        parts << QStringLiteral("%1 likes").arg(QString::number(model.likes));
    }
    if (model.gated) {
        parts << QStringLiteral("gated");
    }
    if (model.privateRepo) {
        parts << QStringLiteral("private");
    }
    parts << QStringLiteral("%1 GGUF file(s)").arg(model.ggufFiles.size());
    return parts.join(QStringLiteral(" | "));
}

} // namespace

HuggingFaceDialog::HuggingFaceDialog(const QString &token,
                                     const QString &downloadDirectory,
                                     int requestTimeoutMs,
                                     QWidget *parent)
    : QDialog(parent)
    , m_searchEdit(new QLineEdit(this))
    , m_searchButton(new QPushButton(QStringLiteral("Search"), this))
    , m_tokenEdit(new QLineEdit(this))
    , m_resultsList(new QListWidget(this))
    , m_filesCombo(new QComboBox(this))
    , m_downloadDirEdit(new QLineEdit(this))
    , m_browseDownloadDirButton(new QPushButton(QStringLiteral("Browse..."), this))
    , m_statusLabel(new QLabel(this))
    , m_downloadButton(new QPushButton(QStringLiteral("Download Selected GGUF"), this))
{
    setWindowTitle(QStringLiteral("Search Hugging Face GGUF"));
    resize(860, 620);

    m_client.setToken(token);
    m_client.setRequestTimeoutMs(requestTimeoutMs);

    m_searchEdit->setPlaceholderText(QStringLiteral("Example: qwen 3b instruct q4"));
    m_tokenEdit->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_tokenEdit->setPlaceholderText(QStringLiteral("Optional, but useful for gated repos"));
    m_tokenEdit->setText(token);

    m_resultsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_filesCombo->setEnabled(false);
    m_downloadDirEdit->setText(downloadDirectory);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setText(QStringLiteral("Search public or gated Hugging Face repositories and download a GGUF into a local model folder."));
    m_downloadButton->setEnabled(false);

    auto *searchRow = new QHBoxLayout;
    searchRow->addWidget(new QLabel(QStringLiteral("Search"), this));
    searchRow->addWidget(m_searchEdit, 1);
    searchRow->addWidget(m_searchButton);

    auto *tokenRow = new QHBoxLayout;
    tokenRow->addWidget(new QLabel(QStringLiteral("HF Token"), this));
    tokenRow->addWidget(m_tokenEdit, 1);

    auto *fileRow = new QHBoxLayout;
    fileRow->addWidget(new QLabel(QStringLiteral("GGUF File"), this));
    fileRow->addWidget(m_filesCombo, 1);

    auto *downloadDirRow = new QHBoxLayout;
    downloadDirRow->addWidget(new QLabel(QStringLiteral("Download To"), this));
    downloadDirRow->addWidget(m_downloadDirEdit, 1);
    downloadDirRow->addWidget(m_browseDownloadDirButton);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->addButton(m_downloadButton, QDialogButtonBox::ActionRole);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(searchRow);
    layout->addLayout(tokenRow);
    layout->addWidget(new QLabel(QStringLiteral("Matching repositories with GGUF files"), this));
    layout->addWidget(m_resultsList, 1);
    layout->addLayout(fileRow);
    layout->addLayout(downloadDirRow);
    layout->addWidget(m_statusLabel);
    layout->addWidget(buttons);

    connect(m_searchButton, &QPushButton::clicked, this, &HuggingFaceDialog::runSearch);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &HuggingFaceDialog::runSearch);
    connect(m_resultsList, &QListWidget::currentRowChanged, this, [this](int) { handleSelectedModelChanged(); });
    connect(m_browseDownloadDirButton, &QPushButton::clicked, this, &HuggingFaceDialog::browseDownloadDirectory);
    connect(m_downloadButton, &QPushButton::clicked, this, &HuggingFaceDialog::downloadSelectedFile);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString HuggingFaceDialog::downloadedModelPath() const
{
    return m_downloadedModelPath;
}

QString HuggingFaceDialog::token() const
{
    return m_tokenEdit->text().trimmed();
}

void HuggingFaceDialog::browseDownloadDirectory()
{
    const QString selectedDir = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Select download folder"),
        m_downloadDirEdit->text().trimmed());
    if (!selectedDir.isEmpty()) {
        m_downloadDirEdit->setText(selectedDir);
    }
}

void HuggingFaceDialog::runSearch()
{
    const QString query = m_searchEdit->text().trimmed();
    if (query.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Search required"), QStringLiteral("Enter a search term first."));
        return;
    }

    m_client.setToken(token());

    QString errorMessage;
    const QVector<llm_gui::services::HuggingFaceModel> results = m_client.searchModelsSync(query, &errorMessage);
    m_results = results;
    m_resultsList->clear();
    m_filesCombo->clear();
    m_filesCombo->setEnabled(false);
    m_downloadButton->setEnabled(false);

    if (results.isEmpty()) {
        m_statusLabel->setText(errorMessage.isEmpty()
                                   ? QStringLiteral("No GGUF repositories matched that search.")
                                   : errorMessage);
        return;
    }

    for (const auto &model : results) {
        auto *item = new QListWidgetItem(model.repoId, m_resultsList);
        item->setToolTip(summarizeModel(model));
    }

    m_resultsList->setCurrentRow(0);
    m_statusLabel->setText(QStringLiteral("Found %1 GGUF-ready Hugging Face repos.").arg(results.size()));
}

void HuggingFaceDialog::handleSelectedModelChanged()
{
    const llm_gui::services::HuggingFaceModel model = currentModel();
    if (model.repoId.isEmpty()) {
        m_filesCombo->clear();
        m_filesCombo->setEnabled(false);
        m_downloadButton->setEnabled(false);
        return;
    }

    if (model.ggufFiles.isEmpty()) {
        m_client.setToken(token());
        QString errorMessage;
        QVector<llm_gui::services::HuggingFaceFile> files = m_client.fetchGgufFilesSync(model.repoId, &errorMessage);
        if (files.isEmpty()) {
            m_statusLabel->setText(errorMessage.isEmpty()
                                       ? QStringLiteral("This repository did not expose any GGUF files.")
                                       : errorMessage);
            return;
        }

        const int row = m_resultsList->currentRow();
        if (row >= 0 && row < m_results.size()) {
            m_results[row].ggufFiles = files;
            updateSelectedFiles(m_results[row]);
        }
        return;
    }

    updateSelectedFiles(model);
}

void HuggingFaceDialog::downloadSelectedFile()
{
    const llm_gui::services::HuggingFaceModel model = currentModel();
    const QString fileName = m_filesCombo->currentData().toString().trimmed();
    const QString downloadDirectory = m_downloadDirEdit->text().trimmed();

    if (model.repoId.isEmpty() || fileName.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Nothing selected"), QStringLiteral("Choose a repository and GGUF file first."));
        return;
    }
    if (downloadDirectory.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Download folder required"), QStringLiteral("Choose where to save the GGUF file."));
        return;
    }

    const QString destinationPath = QFileInfo(downloadDirectory, QFileInfo(fileName).fileName()).absoluteFilePath();
    if (QFileInfo::exists(destinationPath)) {
        const auto choice = QMessageBox::question(
            this,
            QStringLiteral("Overwrite existing file?"),
            QStringLiteral("%1 already exists. Replace it?").arg(destinationPath));
        if (choice != QMessageBox::Yes) {
            return;
        }
    }

    m_client.setToken(token());
    QString errorMessage;
    if (!m_client.downloadFileSync(this, model.repoId, model.revision, fileName, destinationPath, &errorMessage)) {
        QMessageBox::warning(this,
                             QStringLiteral("Download failed"),
                             errorMessage.isEmpty() ? QStringLiteral("The selected GGUF could not be downloaded.") : errorMessage);
        return;
    }

    m_downloadedModelPath = destinationPath;
    accept();
}

void HuggingFaceDialog::updateSelectedFiles(const llm_gui::services::HuggingFaceModel &model)
{
    m_filesCombo->clear();
    for (const auto &file : model.ggufFiles) {
        const QString label = QStringLiteral("%1 (%2)")
                                  .arg(file.name, formatFileSize(file.sizeBytes));
        m_filesCombo->addItem(label, file.name);
    }

    m_filesCombo->setEnabled(m_filesCombo->count() > 0);
    m_downloadButton->setEnabled(m_filesCombo->count() > 0);
    m_statusLabel->setText(QStringLiteral("%1\n%2").arg(model.repoId, summarizeModel(model)));
}

llm_gui::services::HuggingFaceModel HuggingFaceDialog::currentModel() const
{
    const int row = m_resultsList->currentRow();
    if (row < 0 || row >= m_results.size()) {
        return {};
    }
    return m_results.at(row);
}

} // namespace llm_gui::app
