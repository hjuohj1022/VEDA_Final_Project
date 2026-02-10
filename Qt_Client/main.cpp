#include "mainwindow.h"
#include "StyleHelper.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
    QApplication a(argc, argv);
    a.setStyleSheet(StyleHelper::getMainStyle());
    MainWindow w;
    w.setWindowTitle("Vision VMS");
    w.show();
    return a.exec();
}
