#include "MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Chrome Bookmark Explorer"));
    QApplication::setOrganizationName(QStringLiteral("hellomrli"));

    MainWindow window;
    window.show();
    return QApplication::exec();
}
