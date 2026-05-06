#include "ui/views/MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Game Storage Manager");
    QApplication::setOrganizationName("Game Storage Manager");

    gsm::ui::MainWindow window;
    window.resize(1120, 680);
    window.show();

    return app.exec();
}

