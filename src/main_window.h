#ifndef MARKPAD_SRC_MAIN_WINDOW_H_
#define MARKPAD_SRC_MAIN_WINDOW_H_

#include <QMainWindow>
#include <QMap>
#include <QString>
#include <optional>
#include <vector>

#include "document_dialogs.h"
#include "document_session.h"
#include "markdown_document.h"
#include "settings.h"

class QAction;
class QCloseEvent;
class QPlainTextEdit;
class QStackedWidget;

namespace markpad {

class ContinuousEditor;

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
  bool exportHtml();
  bool applySettings(const Settings& settings, QString* error = nullptr);
  void showSettings();
  void undo();
  void redo();
  ContinuousEditor* continuousEditor() const { return continuous_; }
  bool continuousMode() const { return continuousMode_; }
  bool pagedMode() const { return pagedMode_; }
  bool setContinuousMode(bool enabled);
  bool setPagedMode(bool enabled);
  bool applyContinuousCommand(const DocumentToken& token,
                              const markdown::EditCommand& command,
                              int cursor = -1);

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
  struct ModeSelection {
    DocumentToken token;
    int sourceAnchor;
    int sourcePosition;
    int viewAnchor;
    int viewPosition;
  };
  bool applySource(const std::string& source, int cursor);
  void editorChanged();
  void refreshEditor(int cursor = 0);
  void refreshState();
  void report(const DocumentError& error);
  QString text(const QString& key, const QString& fallback) const;

  DocumentSession session_;
  DocumentDialogs& dialogs_;
  SettingsService settingsService_;
  Settings settings_;
  QMap<QString, QString> languageText_;
  QString settingsDiagnostic_;
  QPlainTextEdit* editor_;
  QAction* undoAction_;
  QAction* redoAction_;
  QAction* cutAction_;
  QAction* copyAction_;
  QAction* markdownAction_;
  QAction* continuousAction_;
  QAction* pagedAction_;
  std::vector<QAction*> headingActions_;
  std::vector<QAction*> inlineActions_;
  QAction* taskAction_;
  QStackedWidget* workspace_;
  ContinuousEditor* continuous_;
  bool continuousMode_ = false;
  bool pagedMode_ = false;
  double zoom_ = 1.0;
  std::optional<ModeSelection> modeSelection_;
  std::vector<Edit> history_;
  size_t historyPosition_ = 0;
  QString editorText_;
  std::optional<Change> pendingChange_;
  bool refreshing_ = false;
};

}  // namespace markpad

#endif  // MARKPAD_SRC_MAIN_WINDOW_H_
