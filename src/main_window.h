#ifndef MARKPAD_SRC_MAIN_WINDOW_H_
#define MARKPAD_SRC_MAIN_WINDOW_H_

#include <QMainWindow>

#include <memory>

namespace markpad {

std::unique_ptr<QMainWindow> createMainWindow();

}  // namespace markpad

#endif  // MARKPAD_SRC_MAIN_WINDOW_H_
