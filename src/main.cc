#include <QApplication>
#include <QCommandLineParser>

#include "application_controller.h"

int main(int argc, char* argv[]) {
  QApplication application(argc, argv);
  QApplication::setApplicationName(QStringLiteral("Markpad"));
  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addPositionalArgument(QStringLiteral("files"),
                               QStringLiteral("Markdown files to open"),
                               QStringLiteral("[files...]"));
  parser.process(application);
  markpad::DocumentDialogs dialogs;
  markpad::ApplicationController controller(dialogs);
  controller.start(parser.positionalArguments());
  return application.exec();
}
