#ifndef MARKPAD_SRC_MAIN_WINDOW_H_
#define MARKPAD_SRC_MAIN_WINDOW_H_

#include <QMainWindow>
#include <QString>
#include <optional>
#include <vector>

#include "document_dialogs.h"
#include "document_session.h"

class QAction;
class QCloseEvent;
class QPlainTextEdit;

namespace markpad {

class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  MainWindow(DocumentFileService& files, DocumentDialogs& dialogs);
  const DocumentSession& session() const { return session_; }
  QPlainTextEdit* editor() const { return editor_; }
  bool reusable() const;
  bool openFile(const QString& path);
  bool saveDocument();
  bool saveDocumentAs();
  void undo();
  void redo();

 signals:
  void newRequested();
  void openRequested(const QString& path);
  void exitRequested();

 protected:
  void closeEvent(QCloseEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  struct Edit {
    std::string before;
    std::string after;
    int beforeCursor;
    int afterCursor;
  };
  struct Change {
    int position;
    int removed;
    int added;
  };
  bool applySource(const std::string& source, int cursor);
  void editorChanged();
  void refreshEditor(int cursor = 0);
  void refreshState();
  void report(const DocumentError& error);

  DocumentSession session_;
  DocumentDialogs& dialogs_;
  QPlainTextEdit* editor_;
  QAction* undoAction_;
  QAction* redoAction_;
  std::vector<Edit> history_;
  size_t historyPosition_ = 0;
  QString editorText_;
  std::optional<Change> pendingChange_;
  bool refreshing_ = false;
};

}  // namespace markpad

#endif  // MARKPAD_SRC_MAIN_WINDOW_H_
