#include "ui/views/MainWindow.h"

#include <QApplication>
#include <QFile>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Game Storage Manager");
    QApplication::setOrganizationName("Game Storage Manager");

    QFile qssFile(":/resources/theme_dark.qss");
    if (qssFile.open(QFile::ReadOnly)) {
        QString styleSheet = QLatin1String(qssFile.readAll());
        app.setStyleSheet(styleSheet);
        qssFile.close();
    }

    gsm::ui::MainWindow window;
    window.resize(1120, 680);
    window.show();

    return app.exec();
}

