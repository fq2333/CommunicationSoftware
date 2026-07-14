#include "CommunicationSoftware.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    CommunicationSoftware w;
    w.show();
    return a.exec();
}
