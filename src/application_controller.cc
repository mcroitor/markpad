#include "application_controller.h"

#include <QApplication>
#include <QFileOpenEvent>

namespace markpad {

ApplicationController::ApplicationController(DocumentDialogs& dialogs)
    : dialogs_(dialogs) {
  qApp->installEventFilter(this);
}

ApplicationController::~ApplicationController() {
  qApp->removeEventFilter(this);
  for (auto* window : windows()) {
    delete window;
  }
}

QList<MainWindow*> ApplicationController::windows() const {
  QList<MainWindow*> result;
  for (const auto& window : windows_) {
    if (window) {
      result.append(window);
    }
  }
  return result;
}

MainWindow* ApplicationController::makeWindow() {
  auto* window = new MainWindow(files_, dialogs_);
  window->setAttribute(Qt::WA_DeleteOnClose);
  windows_.removeIf([](const auto& value) { return value.isNull(); });
  windows_.append(window);
  connect(window, &MainWindow::newRequested, this, [this] { newDocument(); });
  connect(window, &MainWindow::openRequested, this,
          [this, window](const QString& path) { openDocument(path, window); });
  connect(window, &MainWindow::exitRequested, this,
          &ApplicationController::closeAll);
  return window;
}

MainWindow* ApplicationController::newDocument() {
  auto* window = makeWindow();
  window->show();
  return window;
}

MainWindow* ApplicationController::openDocument(const QString& path,
                                                MainWindow* requester) {
  MainWindow* target = requester && requester->reusable() ? requester : nullptr;
  const bool created = !target;
  if (!target) {
    target = makeWindow();
  }
  if (!target->openFile(path)) {
    if (created) {
      delete target;
    }
    return nullptr;
  }
  target->show();
  target->raise();
  target->activateWindow();
  return target;
}

void ApplicationController::start(const QStringList& paths) {
  for (const auto& path : paths) {
    openDocument(path);
  }
  if (windows().isEmpty()) {
    newDocument();
  }
}

void ApplicationController::closeAll() {
  const auto current = windows();
  for (auto* window : current) {
    if (!window->close()) {
      return;
    }
  }
}

bool ApplicationController::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::FileOpen) {
    const auto* file = static_cast<QFileOpenEvent*>(event);
    auto* requester = qobject_cast<MainWindow*>(QApplication::activeWindow());
    if (!requester && windows().size() == 1) {
      requester = windows().front();
    }
    if (!file->file().isEmpty()) {
      openDocument(file->file(), requester);
    } else {
      dialogs_.showError(requester,
                         QStringLiteral("Only local files can be opened"));
    }
    return true;
  }
  return QObject::eventFilter(watched, event);
}

}  // namespace markpad
