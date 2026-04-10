#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QIcon>

#include "app/MainWindow.h"
#include "core/AppSettings.h"
#include "core/SessionStore.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("LLM-GUI");
    QCoreApplication::setOrganizationDomain("llm-gui.local");
    QCoreApplication::setApplicationName("llm-gui");

    app.setWindowIcon(QIcon(":/icons/llm-gui.svg"));

    llm_gui::core::AppSettings settings;
    llm_gui::core::SessionStore sessionStore(settings.sessionStoragePath());

    llm_gui::app::MainWindow window(settings, sessionStore);
    window.show();

    return app.exec();
}
