#include "main_window.h"

#include <QAction>
#include <QCloseEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStringConverter>
#include <QTextCursor>
#include <QToolBar>
#include <algorithm>

#include "markdown_highlighter.h"

namespace markpad {
namespace {

struct Projection {
  QString text;
  std::vector<qsizetype> offsets;
};

QString sourceText(const std::string& source) {
  QStringDecoder decoder(QStringConverter::Utf8,
                         QStringConverter::Flag::Stateless |
                             QStringConverter::Flag::ConvertInitialBom);
  return decoder(QByteArrayView(source.data(), qsizetype(source.size())));
}

Projection project(const QString& source) {
  Projection result;
  result.offsets.push_back(0);
  for (qsizetype i = 0; i < source.size(); ++i) {
    const auto ch = source[i];
    if (ch == '\r') {
      if (i + 1 < source.size() && source[i + 1] == '\n') {
        ++i;
      }
      result.text += '\n';
    } else {
      result.text += ch == QChar::ParagraphSeparator ? QChar('\n') : ch;
    }
    result.offsets.push_back(i + 1);
  }
  return result;
}

QString lineEnding(const QString& source) {
  for (qsizetype i = 0; i < source.size(); ++i) {
    if (source[i] == '\r') {
      return i + 1 < source.size() && source[i + 1] == '\n'
                 ? QStringLiteral("\r\n")
                 : QStringLiteral("\r");
    }
    if (source[i] == '\n') {
      return QStringLiteral("\n");
    }
  }
  return QStringLiteral("\n");
}

QString rawEditorText(QPlainTextEdit* editor) {
  auto text = editor->document()->toRawText();
  text.replace(QChar::ParagraphSeparator, QChar('\n'));
  return text;
}

}  // namespace

MainWindow::MainWindow(DocumentFileService& files, DocumentDialogs& dialogs)
    : session_(files), dialogs_(dialogs), editor_(new QPlainTextEdit(this)) {
  resize(1000, 700);
  editor_->setObjectName(QStringLiteral("markdownEditor"));
  editor_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  editor_->setUndoRedoEnabled(false);
  editor_->installEventFilter(this);
  setCentralWidget(editor_);
  new MarkdownHighlighter(editor_->document());
  auto* file = menuBar()->addMenu(QStringLiteral("&File"));
  auto* toolbar = addToolBar(QStringLiteral("File"));
  toolbar->setObjectName(QStringLiteral("fileToolbar"));
  const auto action = [&](const QString& text, const QString& name,
                          const QKeySequence& shortcut, const auto& handler) {
    auto* value = file->addAction(text);
    value->setObjectName(name);
    value->setShortcut(shortcut);
    connect(value, &QAction::triggered, this, handler);
    return value;
  };
  toolbar->addAction(action(QStringLiteral("&New"), QStringLiteral("newAction"),
                            QKeySequence::New,
                            [this] { emit newRequested(); }));
  toolbar->addAction(action(QStringLiteral("&Open..."),
                            QStringLiteral("openAction"), QKeySequence::Open,
                            [this] {
                              const auto path = dialogs_.chooseOpen(this);
                              if (!path.isEmpty()) {
                                emit openRequested(path);
                              }
                            }));
  toolbar->addAction(action(QStringLiteral("&Save"),
                            QStringLiteral("saveAction"), QKeySequence::Save,
                            [this] { saveDocument(); }));
  action(QStringLiteral("Save &As..."), QStringLiteral("saveAsAction"),
         QKeySequence::SaveAs, [this] { saveDocumentAs(); });
  file->addSeparator();
  action(QStringLiteral("&Close"), QStringLiteral("closeAction"),
         QKeySequence::Close, [this] { close(); });
  action(QStringLiteral("E&xit"), QStringLiteral("exitAction"),
         QKeySequence::Quit, [this] { emit exitRequested(); });

  auto* edit = menuBar()->addMenu(QStringLiteral("&Edit"));
  undoAction_ = edit->addAction(QStringLiteral("&Undo"));
  undoAction_->setObjectName(QStringLiteral("undoAction"));
  undoAction_->setShortcut(QKeySequence::Undo);
  connect(undoAction_, &QAction::triggered, this, &MainWindow::undo);
  redoAction_ = edit->addAction(QStringLiteral("&Redo"));
  redoAction_->setObjectName(QStringLiteral("redoAction"));
  redoAction_->setShortcut(QKeySequence::Redo);
  connect(redoAction_, &QAction::triggered, this, &MainWindow::redo);
  edit->addSeparator();
  const auto editAction = [&](const QString& name,
                              QKeySequence::StandardKey key, auto slot) {
    auto* value = edit->addAction(name);
    value->setShortcut(key);
    connect(value, &QAction::triggered, editor_, slot);
    return value;
  };
  auto* cut = editAction(QStringLiteral("Cu&t"), QKeySequence::Cut,
                         &QPlainTextEdit::cut);
  auto* copy = editAction(QStringLiteral("&Copy"), QKeySequence::Copy,
                          &QPlainTextEdit::copy);
  cut->setEnabled(false);
  copy->setEnabled(false);
  connect(editor_, &QPlainTextEdit::copyAvailable, cut, &QAction::setEnabled);
  connect(editor_, &QPlainTextEdit::copyAvailable, copy, &QAction::setEnabled);
  editAction(QStringLiteral("&Paste"), QKeySequence::Paste,
             &QPlainTextEdit::paste);
  editAction(QStringLiteral("Select &All"), QKeySequence::SelectAll,
             &QPlainTextEdit::selectAll);
  editor_->addActions(edit->actions());
  editor_->setContextMenuPolicy(Qt::ActionsContextMenu);
  auto* view = menuBar()->addMenu(QStringLiteral("&View"));
  view->addAction(toolbar->toggleViewAction());
  auto* status = view->addAction(QStringLiteral("Status bar"));
  status->setCheckable(true);
  status->setChecked(true);
  connect(status, &QAction::toggled, statusBar(), &QStatusBar::setVisible);
  auto* help = menuBar()->addMenu(QStringLiteral("&Help"));
  connect(help->addAction(QStringLiteral("About Markpad")), &QAction::triggered,
          this, [this] {
            QMessageBox::about(
                this, QStringLiteral("Markpad"),
                QStringLiteral("Markpad - Markdown source editor"));
          });
  connect(editor_->document(), &QTextDocument::contentsChange, this,
          [this](int position, int removed, int added) {
            if (!refreshing_ && (removed != 0 || added != 0)) {
              pendingChange_ = Change{position, removed, added};
            }
          });
  connect(editor_, &QPlainTextEdit::textChanged, this,
          &MainWindow::editorChanged);
  connect(editor_, &QPlainTextEdit::cursorPositionChanged, this,
          &MainWindow::refreshState);
  refreshEditor();
}

bool MainWindow::reusable() const {
  return session_.path().empty() && !session_.dirty() &&
         session_.snapshot().source.empty();
}

void MainWindow::report(const DocumentError& error) {
  dialogs_.showError(this, QString::fromUtf8(error.message));
}

bool MainWindow::openFile(const QString& path) {
  auto error = session_.open(filePath(path));
  if (error && error->code == DocumentErrorCode::kEncoding) {
    report(*error);
    const auto encoding = dialogs_.chooseEncoding(this);
    if (!encoding) {
      return false;
    }
    error = session_.open(filePath(path), *encoding);
  }
  if (error) {
    report(*error);
    return false;
  }
  history_.clear();
  historyPosition_ = 0;
  refreshEditor();
  return true;
}

bool MainWindow::saveDocument() {
  if (session_.path().empty()) {
    return saveDocumentAs();
  }
  if (auto error = session_.save()) {
    report(*error);
    return false;
  }
  refreshState();
  return true;
}

bool MainWindow::saveDocumentAs() {
  const auto selected = dialogs_.chooseSave(this, displayPath(session_.path()),
                                            session_.snapshot().format);
  if (!selected) {
    return false;
  }
  if (auto error =
          session_.saveAs(filePath(selected->path), selected->format)) {
    report(*error);
    return false;
  }
  refreshState();
  return true;
}

void MainWindow::refreshEditor(int position) {
  const auto snapshot = session_.snapshot();
  editorText_ = project(sourceText(snapshot.source)).text;
  const QSignalBlocker blocker(editor_);
  refreshing_ = true;
  if (rawEditorText(editor_) != editorText_) {
    editor_->setPlainText(editorText_);
  }
  auto cursor = editor_->textCursor();
  cursor.setPosition(std::clamp(position, 0, int(editorText_.size())));
  editor_->setTextCursor(cursor);
  refreshing_ = false;
  pendingChange_.reset();
  refreshState();
}

void MainWindow::refreshState() {
  const auto path = displayPath(session_.path());
  const auto name =
      path.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(path).fileName();
  setWindowTitle(name + QStringLiteral("[*] - Markpad"));
  setWindowModified(session_.dirty());
  setWindowFilePath(path);
  undoAction_->setEnabled(historyPosition_ > 0);
  redoAction_->setEnabled(historyPosition_ < history_.size());
  const auto cursor = editor_->textCursor();
  statusBar()->showMessage(QStringLiteral("Markdown | Ln %1, Col %2 | %3")
                               .arg(cursor.blockNumber() + 1)
                               .arg(cursor.positionInBlock() + 1)
                               .arg(session_.dirty()
                                        ? QStringLiteral("Modified")
                                        : QStringLiteral("Saved")));
}

bool MainWindow::applySource(const std::string& source, int cursor) {
  if (auto error = session_.replaceSource(session_.snapshot().token, source)) {
    report(*error);
    refreshEditor();
    return false;
  }
  refreshEditor(cursor);
  return true;
}

void MainWindow::editorChanged() {
  const auto text = rawEditorText(editor_);
  const auto change = pendingChange_;
  pendingChange_.reset();
  if (text == editorText_) {
    return;
  }
  const auto before = session_.snapshot().source;
  const auto raw = sourceText(before);
  const auto projection = project(raw);
  if (!change || change->position < 0 ||
      change->position > editorText_.size() || change->position > text.size()) {
    dialogs_.showError(this,
                       QStringLiteral("Cannot map editor change to source"));
    refreshEditor();
    return;
  }
  const qsizetype prefix = change->position;
  const auto removed =
      std::min(qsizetype(change->removed), editorText_.size() - prefix);
  const auto added = std::min(qsizetype(change->added), text.size() - prefix);
  if (text.left(prefix) != editorText_.left(prefix) ||
      text.mid(prefix + added) != editorText_.mid(prefix + removed)) {
    dialogs_.showError(this,
                       QStringLiteral("Editor change mapping is inconsistent"));
    refreshEditor();
    return;
  }
  auto inserted = text.mid(prefix, added);
  inserted.replace(QStringLiteral("\n"), lineEnding(raw));
  auto changed = raw;
  const auto first = projection.offsets[size_t(prefix)];
  const auto last = projection.offsets[size_t(prefix + removed)];
  changed.replace(first, last - first, inserted);
  const auto after = changed.toUtf8().toStdString();
  if (!changed.isValidUtf16()) {
    dialogs_.showError(this, QStringLiteral("Invalid Unicode input"));
    refreshEditor(int(prefix));
    return;
  }
  const auto position = editor_->textCursor().position();
  if (!applySource(after, position)) {
    return;
  }
  history_.resize(historyPosition_);
  history_.push_back({before, after, int(prefix), position});
  historyPosition_ = history_.size();
  refreshState();
}

void MainWindow::undo() {
  if (historyPosition_ == 0) {
    return;
  }
  const auto& edit = history_[historyPosition_ - 1];
  if (applySource(edit.before, edit.beforeCursor)) {
    --historyPosition_;
    refreshState();
  }
}

void MainWindow::redo() {
  if (historyPosition_ == history_.size()) {
    return;
  }
  const auto& edit = history_[historyPosition_];
  if (applySource(edit.after, edit.afterCursor)) {
    ++historyPosition_;
    refreshState();
  }
}

void MainWindow::closeEvent(QCloseEvent* event) {
  if (session_.dirty()) {
    const auto choice = dialogs_.confirmClose(this, windowTitle());
    if (choice == CloseChoice::kCancel ||
        (choice == CloseChoice::kSave && !saveDocument())) {
      event->ignore();
      return;
    }
  }
  event->accept();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched == editor_ && (event->type() == QEvent::ShortcutOverride ||
                             event->type() == QEvent::KeyPress)) {
    auto* key = static_cast<QKeyEvent*>(event);
    const bool isUndo = key->matches(QKeySequence::Undo);
    const bool isRedo = key->matches(QKeySequence::Redo);
    if (isUndo || isRedo) {
      if (event->type() == QEvent::KeyPress) {
        if (isUndo) {
          undo();
        } else {
          redo();
        }
      }
      event->accept();
      return true;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

}  // namespace markpad
