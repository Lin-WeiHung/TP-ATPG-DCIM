// cmake --build qt_faultsim/build -j
// QT_QPA_PLATFORM=xcb ./qt_faultsim/build/faultsim_gui
#include <QApplication>
#include "MainWindow.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    MainWindow w;
    w.resize(900, 600);
    w.show();
    return app.exec();
}
