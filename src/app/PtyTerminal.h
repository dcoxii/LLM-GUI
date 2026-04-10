#pragma once

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QWidget>

#ifndef LLM_GUI_HAS_QTERMWIDGET
#include <sys/types.h>
#endif

#ifdef LLM_GUI_HAS_QTERMWIDGET
class QTermWidget;
#else
class QPlainTextEdit;
class QSocketNotifier;
class QResizeEvent;
#endif

namespace llm_gui::app {

#ifndef LLM_GUI_HAS_QTERMWIDGET
class TerminalView;
#endif

class PtyTerminal final : public QWidget
{
    Q_OBJECT

public:
    explicit PtyTerminal(QWidget *parent = nullptr);
    ~PtyTerminal() override;

    bool startShell(const QString &program = QStringLiteral("/bin/bash"),
                    const QStringList &arguments = { QStringLiteral("--norc") });

public slots:
    void sendCommand(const QString &command);

#ifndef LLM_GUI_HAS_QTERMWIDGET
    void writeBytes(const QByteArray &bytes);

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void readPty();

private:
    void updateWindowSize();
    void cleanup();

    int m_masterFd { -1 };
    int m_slaveFd { -1 };
    pid_t m_childPid { -1 };
    QSocketNotifier *m_notifier { nullptr };
    TerminalView *m_view { nullptr };
#else
private:
    QTermWidget *m_terminal { nullptr };
    bool m_started { false };
#endif
};

} // namespace llm_gui::app
