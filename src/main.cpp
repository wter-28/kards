#include <QApplication>

#include "mainwindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("KardsAutoBot"));
    QApplication::setOrganizationName(QStringLiteral("wter"));

    MainWindow window;
    window.show();

    return app.exec();
}
