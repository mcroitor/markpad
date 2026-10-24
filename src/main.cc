#include "main_window.h"

#include <QApplication>
#include <QMainWindow>

int main(int argc, char* argv[]) {
  QApplication application(argc, argv);
  auto window = markpad::createMainWindow();
  window->show();
  return application.exec();
}
