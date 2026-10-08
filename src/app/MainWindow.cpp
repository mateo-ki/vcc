#include "app/MainWindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("VPN Parser"));
    resize(1280, 800);
}
