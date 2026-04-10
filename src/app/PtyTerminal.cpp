#include "app/PtyTerminal.h"

#ifdef LLM_GUI_HAS_QTERMWIDGET

#include <QFontDatabase>
#include <QVBoxLayout>
#include <qtermwidget6/qtermwidget.h>

namespace llm_gui::app {

PtyTerminal::PtyTerminal(QWidget *parent)
    : QWidget(parent)
    , m_terminal(new QTermWidget(0, this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_terminal);

    m_terminal->setAutoClose(false);
    m_terminal->setScrollBarPosition(QTermWidget::ScrollBarRight);
    m_terminal->setColorScheme(QStringLiteral("Linux"));
    m_terminal->setTerminalSizeHint(true);
    m_terminal->setFlowControlWarningEnabled(false);

    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_terminal->setTerminalFont(font);
}

PtyTerminal::~PtyTerminal() = default;

bool PtyTerminal::startShell(const QString &program, const QStringList &arguments)
{
    if (m_started || !m_terminal) {
        return true;
    }

    QStringList args = arguments;
    m_terminal->setShellProgram(program);
    m_terminal->setArgs(args);
    m_terminal->startShellProgram();
    m_terminal->setFocus();
    m_started = true;
    return true;
}

void PtyTerminal::sendCommand(const QString &command)
{
    if (!m_terminal) {
        return;
    }

    QString text = command;
    if (!text.endsWith(QLatin1Char('\n'))) {
        text.append(QLatin1Char('\n'));
    }
    m_terminal->sendText(text);
}

} // namespace llm_gui::app

#else

#include <QFontDatabase>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSocketNotifier>
#include <QTextCursor>
#include <QTextOption>
#include <QVBoxLayout>
#include <QtGlobal>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <vector>

#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace llm_gui::app {

class TerminalView final : public QPlainTextEdit
{
public:
    explicit TerminalView(PtyTerminal *terminal, QWidget *parent = nullptr)
        : QPlainTextEdit(parent)
        , m_terminal(terminal)
    {
        setUndoRedoEnabled(false);
        setLineWrapMode(QPlainTextEdit::NoWrap);
        setWordWrapMode(QTextOption::NoWrap);
        setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        setCursorWidth(2);
        setPlaceholderText(QStringLiteral("Embedded terminal"));
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (!m_terminal) {
            QPlainTextEdit::keyPressEvent(event);
            return;
        }

        const Qt::KeyboardModifiers modifiers = event->modifiers();
        if (modifiers.testFlag(Qt::ControlModifier) && !event->text().isEmpty()) {
            const QChar ch = event->text().at(0);
            if (ch.isLetter()) {
                const char control = static_cast<char>(ch.toUpper().unicode() - 'A' + 1);
                m_terminal->writeBytes(QByteArray(1, control));
                return;
            }
        }

        switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            m_terminal->writeBytes("\r");
            return;
        case Qt::Key_Backspace:
            m_terminal->writeBytes("\x7f");
            return;
        case Qt::Key_Tab:
            m_terminal->writeBytes("\t");
            return;
        case Qt::Key_Escape:
            m_terminal->writeBytes("\x1b");
            return;
        case Qt::Key_Left:
            m_terminal->writeBytes("\x1b[D");
            return;
        case Qt::Key_Right:
            m_terminal->writeBytes("\x1b[C");
            return;
        case Qt::Key_Up:
            m_terminal->writeBytes("\x1b[A");
            return;
        case Qt::Key_Down:
            m_terminal->writeBytes("\x1b[B");
            return;
        case Qt::Key_Home:
            m_terminal->writeBytes("\x1b[H");
            return;
        case Qt::Key_End:
            m_terminal->writeBytes("\x1b[F");
            return;
        case Qt::Key_Delete:
            m_terminal->writeBytes("\x1b[3~");
            return;
        case Qt::Key_PageUp:
            m_terminal->writeBytes("\x1b[5~");
            return;
        case Qt::Key_PageDown:
            m_terminal->writeBytes("\x1b[6~");
            return;
        default:
            break;
        }

        const QString text = event->text();
        if (!text.isEmpty()) {
            m_terminal->writeBytes(text.toUtf8());
            return;
        }

        QPlainTextEdit::keyPressEvent(event);
    }

private:
    PtyTerminal *m_terminal;
};

PtyTerminal::PtyTerminal(QWidget *parent)
    : QWidget(parent)
    , m_view(new TerminalView(this, this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);
}

PtyTerminal::~PtyTerminal()
{
    cleanup();
}

bool PtyTerminal::startShell(const QString &program, const QStringList &arguments)
{
    if (m_childPid > 0) {
        return true;
    }

    struct winsize ws {};
    ws.ws_row = 24;
    ws.ws_col = 80;

    if (::openpty(&m_masterFd, &m_slaveFd, nullptr, nullptr, &ws) != 0) {
        m_view->appendPlainText(QStringLiteral("openpty failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }

    m_childPid = ::fork();
    if (m_childPid < 0) {
        m_view->appendPlainText(QStringLiteral("fork failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        cleanup();
        return false;
    }

    if (m_childPid == 0) {
        ::close(m_masterFd);
        ::setsid();
        ::ioctl(m_slaveFd, TIOCSCTTY, 0);
        ::dup2(m_slaveFd, STDIN_FILENO);
        ::dup2(m_slaveFd, STDOUT_FILENO);
        ::dup2(m_slaveFd, STDERR_FILENO);
        if (m_slaveFd > STDERR_FILENO) {
            ::close(m_slaveFd);
        }

        QByteArray programBytes = program.toUtf8();
        std::vector<QByteArray> argStorage;
        argStorage.reserve(static_cast<size_t>(arguments.size()) + 1);
        argStorage.push_back(programBytes);
        for (const QString &argument : arguments) {
            argStorage.push_back(argument.toUtf8());
        }

        std::vector<char *> argv;
        argv.reserve(argStorage.size() + 1);
        for (QByteArray &arg : argStorage) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);

        ::setenv("TERM", "xterm-256color", 1);
        ::execvp(programBytes.constData(), argv.data());
        _exit(127);
    }

    ::close(m_slaveFd);
    m_slaveFd = -1;

    m_notifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &PtyTerminal::readPty);

    updateWindowSize();
    m_view->setFocus();
    return true;
}

void PtyTerminal::writeBytes(const QByteArray &bytes)
{
    if (m_masterFd < 0 || bytes.isEmpty()) {
        return;
    }

    const char *data = bytes.constData();
    qsizetype remaining = bytes.size();
    while (remaining > 0) {
        const ssize_t written = ::write(m_masterFd, data, static_cast<size_t>(remaining));
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        data += written;
        remaining -= written;
    }
}

void PtyTerminal::sendCommand(const QString &command)
{
    writeBytes(command.toUtf8());
    writeBytes("\n");
}

void PtyTerminal::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateWindowSize();
}

void PtyTerminal::readPty()
{
    if (m_masterFd < 0) {
        return;
    }

    char buffer[4096];
    const ssize_t count = ::read(m_masterFd, buffer, sizeof(buffer));
    if (count > 0) {
        m_view->moveCursor(QTextCursor::End);
        m_view->insertPlainText(QString::fromLocal8Bit(buffer, static_cast<int>(count)));
        m_view->moveCursor(QTextCursor::End);
        if (m_view->verticalScrollBar()) {
            m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->maximum());
        }
        return;
    }

    if (count == 0 || (count < 0 && errno != EINTR && errno != EAGAIN)) {
        if (m_notifier) {
            m_notifier->setEnabled(false);
        }
    }
}

void PtyTerminal::updateWindowSize()
{
    if (m_masterFd < 0 || !m_view) {
        return;
    }

    const QFontMetrics metrics(m_view->font());
    const int charWidth = qMax(1, metrics.horizontalAdvance(QLatin1Char('M')));
    const int charHeight = qMax(1, metrics.height());

    struct winsize ws {};
    ws.ws_col = static_cast<unsigned short>(qMax(20, m_view->viewport()->width() / charWidth));
    ws.ws_row = static_cast<unsigned short>(qMax(5, m_view->viewport()->height() / charHeight));
    ws.ws_xpixel = static_cast<unsigned short>(m_view->viewport()->width());
    ws.ws_ypixel = static_cast<unsigned short>(m_view->viewport()->height());

    ::ioctl(m_masterFd, TIOCSWINSZ, &ws);
    if (m_childPid > 0) {
        ::kill(m_childPid, SIGWINCH);
    }
}

void PtyTerminal::cleanup()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }

    if (m_masterFd >= 0) {
        ::close(m_masterFd);
        m_masterFd = -1;
    }

    if (m_slaveFd >= 0) {
        ::close(m_slaveFd);
        m_slaveFd = -1;
    }

    if (m_childPid > 0) {
        ::kill(m_childPid, SIGHUP);
        ::waitpid(m_childPid, nullptr, 0);
        m_childPid = -1;
    }
}

} // namespace llm_gui::app

#endif
