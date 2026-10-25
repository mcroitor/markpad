#ifndef MARKPAD_SRC_APPLICATION_CONTROLLER_H_
#define MARKPAD_SRC_APPLICATION_CONTROLLER_H_

#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>

#include "main_window.h"
#include "qt_document_file_service.h"

namespace markpad {

class ApplicationController final : public QObject {
 public:
  explicit ApplicationController(DocumentDialogs& dialogs);
  ~ApplicationController() override;
  MainWindow* newDocument();
  MainWindow* openDocument(const QString& path,
                           MainWindow* requester = nullptr);
  void start(const QStringList& paths);
  void closeAll();
  QList<MainWindow*> windows() const;

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  MainWindow* makeWindow();
  DocumentDialogs& dialogs_;
  QtDocumentFileService files_;
  QList<QPointer<MainWindow>> windows_;
};

}  // namespace markpad

#endif  // MARKPAD_SRC_APPLICATION_CONTROLLER_H_
