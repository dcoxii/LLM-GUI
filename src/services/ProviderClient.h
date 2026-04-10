#pragma once

#include <QObject>
#include <QString>

namespace llm_gui::services {

class ProviderClient : public QObject
{
    Q_OBJECT

public:
    explicit ProviderClient(QObject *parent = nullptr) : QObject(parent) {}
    ~ProviderClient() override = default;

    virtual QString providerId() const = 0;
    virtual QString displayName() const = 0;
    virtual QString model() const = 0;
    virtual void setModel(const QString &model) = 0;
    virtual void sendPrompt(const QString &prompt, bool stream) = 0;
    virtual bool sendPromptSync(const QString &prompt, QString *response, QString *errorMessage) = 0;
    virtual void cancel() = 0;

signals:
    void streamStarted();
    void tokenReceived(const QString &token);
    void streamFinished();
    void requestFailed(const QString &message);
    void statusChanged(const QString &status);
};

} // namespace llm_gui::services
