#include "main_window.h"

#include <QMainWindow>

namespace markpad {

std::unique_ptr<QMainWindow> createMainWindow() {
  auto window = std::make_unique<QMainWindow>();
  window->setWindowTitle(QStringLiteral("Markpad"));
  window->resize(1000, 700);
  return window;
}

}  // namespace markpad
