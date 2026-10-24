#include "main_window.h"

#include <QMainWindow>
#include <QtTest/QtTest>

#include <memory>

class MainWindowSmokeTest : public QObject {
  Q_OBJECT

 private slots:
  void createsEmptyMainWindow() {
    auto window = markpad::createMainWindow();

    QCOMPARE(window->windowTitle(), QStringLiteral("Markpad"));
    QVERIFY(window->centralWidget() == nullptr);

    window->show();
    QVERIFY(window->isVisible());
  }
};

QTEST_MAIN(MainWindowSmokeTest)

#include "smoke_test.moc"
