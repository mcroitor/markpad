#include "main_window.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QList>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPageLayout>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPrintDialog>
#include <QPrinter>
#include <QSignalBlocker>
#include <QSize>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStringConverter>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <algorithm>

#include "continuous_editor.h"
#include "html_export.h"
#include "markdown_highlighter.h"
#include "pdf_export.h"
#include "qt_html_export_service.h"
#include "settings_dialog.h"
#include "toolbar_icons.h"

namespace markpad {
namespace {

std::optional<ContinuousEditor::TableAction> tableActionKind(
    const QString& name) {
  if (name == QStringLiteral("tableInsertRowAboveAction")) {
    return ContinuousEditor::TableAction::kInsertRowAbove;
  }
  if (name == QStringLiteral("tableInsertRowBelowAction")) {
    return ContinuousEditor::TableAction::kInsertRowBelow;
  }
  if (name == QStringLiteral("tableRemoveRowAction")) {
    return ContinuousEditor::TableAction::kRemoveRow;
  }
  if (name == QStringLiteral("tableInsertColumnLeftAction")) {
    return ContinuousEditor::TableAction::kInsertColumnLeft;
  }
  if (name == QStringLiteral("tableInsertColumnRightAction")) {
    return ContinuousEditor::TableAction::kInsertColumnRight;
  }
  if (name == QStringLiteral("tableRemoveColumnAction")) {
    return ContinuousEditor::TableAction::kRemoveColumn;
  }
  if (name == QStringLiteral("tableAlignLeftAction")) {
    return ContinuousEditor::TableAction::kAlignLeft;
  }
  if (name == QStringLiteral("tableAlignCenterAction")) {
    return ContinuousEditor::TableAction::kAlignCenter;
  }
  if (name == QStringLiteral("tableAlignRightAction")) {
    return ContinuousEditor::TableAction::kAlignRight;
  }
  if (name == QStringLiteral("tableRemoveTableAction")) {
    return ContinuousEditor::TableAction::kRemoveTable;
  }
  return std::nullopt;
}

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
  const auto loaded = settingsService_.load();
  settings_ = loaded.settings;
  languageText_ = settingsService_.languageMap(settings_.language);
  settingsDiagnostic_ = loaded.diagnostic;
  dialogs_.setDefaultLocation(settings_.defaultFileLocation);
  resize(1000, 700);
  editor_->setObjectName(QStringLiteral("markdownEditor"));
  editor_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  editor_->setUndoRedoEnabled(false);
  editor_->installEventFilter(this);
  workspace_ = new QStackedWidget(this);
  continuous_ = new ContinuousEditor(this);
  workspace_->addWidget(editor_);
  workspace_->addWidget(continuous_);
  setCentralWidget(workspace_);
  continuous_->editor()->installEventFilter(this);
  new MarkdownHighlighter(editor_->document());
  auto* file = menuBar()->addMenu(text(QStringLiteral("menu.file"),
                                      QStringLiteral("&File")));
  file->setObjectName(QStringLiteral("fileMenu"));
  auto* toolbar = addToolBar(QStringLiteral("File"));
  toolbar->setObjectName(QStringLiteral("mainToolBar"));
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
  auto* exportMenu = file->addMenu(
      text(QStringLiteral("menu.export"), QStringLiteral("&Export")));
  exportMenu->setObjectName(QStringLiteral("exportMenu"));
  exportMenu->menuAction()->setObjectName(QStringLiteral("exportAction"));
  auto* exportPdfAction = exportMenu->addAction(
      text(QStringLiteral("action.exportPdf"), QStringLiteral("PDF")));
  exportPdfAction->setObjectName(QStringLiteral("exportPdfAction"));
  connect(exportPdfAction, &QAction::triggered, this,
          [this] { exportPdf(); });
  auto* exportHtmlAction = exportMenu->addAction(
      text(QStringLiteral("action.exportHtml"), QStringLiteral("HTML")));
  exportHtmlAction->setObjectName(QStringLiteral("exportHtmlAction"));
  connect(exportHtmlAction, &QAction::triggered, this,
          [this] { exportHtml(); });
  auto* print = file->addAction(
      text(QStringLiteral("action.print"), QStringLiteral("&Print")));
  print->setObjectName(QStringLiteral("printAction"));
  connect(print, &QAction::triggered, this, [this] { printDocument(); });
  file->addSeparator();
  action(text(QStringLiteral("action.exit"), QStringLiteral("E&xit")),
         QStringLiteral("exitAction"), QKeySequence::Quit,
         [this] { emit exitRequested(); });

  auto* edit = menuBar()->addMenu(
      text(QStringLiteral("menu.edit"), QStringLiteral("&Edit")));
  edit->setObjectName(QStringLiteral("editMenu"));
  undoAction_ = edit->addAction(
      text(QStringLiteral("action.undo"), QStringLiteral("&Undo")));
  undoAction_->setObjectName(QStringLiteral("undoAction"));
  undoAction_->setShortcut(QKeySequence::Undo);
  connect(undoAction_, &QAction::triggered, this, &MainWindow::undo);
  redoAction_ = edit->addAction(
      text(QStringLiteral("action.redo"), QStringLiteral("&Redo")));
  redoAction_->setObjectName(QStringLiteral("redoAction"));
  redoAction_->setShortcut(QKeySequence::Redo);
  connect(redoAction_, &QAction::triggered, this, &MainWindow::redo);
  const auto editAction = [&](const QString& name,
                              QKeySequence::StandardKey key, auto plainSlot,
                              auto richSlot) {
    auto* value = edit->addAction(name);
    value->setShortcut(key);
    connect(value, &QAction::triggered, this, [this, plainSlot, richSlot] {
      if (continuousMode_) {
        (continuous_->editor()->*richSlot)();
      } else {
        (editor_->*plainSlot)();
      }
    });
    return value;
  };
  cutAction_ = editAction(
      text(QStringLiteral("action.cut"), QStringLiteral("Cu&t")),
      QKeySequence::Cut, &QPlainTextEdit::cut, &QTextEdit::cut);
  cutAction_->setObjectName(QStringLiteral("cutAction"));
  copyAction_ = editAction(
      text(QStringLiteral("action.copy"), QStringLiteral("&Copy")),
      QKeySequence::Copy, &QPlainTextEdit::copy, &QTextEdit::copy);
  copyAction_->setObjectName(QStringLiteral("copyAction"));
  connect(editor_, &QPlainTextEdit::copyAvailable, this,
          &MainWindow::refreshState);
  auto* pasteAction = editAction(
      text(QStringLiteral("action.paste"), QStringLiteral("&Paste")),
      QKeySequence::Paste, &QPlainTextEdit::paste, &QTextEdit::paste);
  pasteAction->setObjectName(QStringLiteral("pasteAction"));
  auto* selectAllAction = new QAction(
      text(QStringLiteral("action.selectAll"), QStringLiteral("Select &All")),
      this);
  selectAllAction->setObjectName(QStringLiteral("selectAllAction"));
  selectAllAction->setShortcut(QKeySequence::SelectAll);
  connect(selectAllAction, &QAction::triggered, this, [this] {
    if (continuousMode_) {
      continuous_->editor()->selectAll();
    } else {
      editor_->selectAll();
    }
  });
  edit->addSeparator();
  auto* settingsAction = edit->addAction(
      text(QStringLiteral("action.settings"), QStringLiteral("&Settings")));
  settingsAction->setObjectName(QStringLiteral("settingsAction"));
  connect(settingsAction, &QAction::triggered, this,
          [this] { showSettings(); });
  auto* format = menuBar()->addMenu(
      text(QStringLiteral("menu.format"), QStringLiteral("F&ormat")));
  format->setObjectName(QStringLiteral("formatMenu"));
  auto* headings = format->addMenu(
      text(QStringLiteral("action.heading"), QStringLiteral("Heading")));
  headings->setObjectName(QStringLiteral("headingMenu"));
  headings->menuAction()->setObjectName(QStringLiteral("headingMenuAction"));
  for (int level = 0; level <= 6; ++level) {
    QAction* value = nullptr;
    if (level == 0) {
      value = new QAction(
          text(QStringLiteral("action.paragraph"),
               QStringLiteral("Paragraph")),
          this);
    } else {
      value = headings->addAction(QStringLiteral("H%1").arg(level));
    }
    value->setObjectName(QStringLiteral("heading%1Action").arg(level));
    headingActions_.push_back(value);
    connect(value, &QAction::triggered, this, [this, level] {
      const auto node = continuous_->headingNode();
      if (node) {
        applyContinuousCommand(continuous_->token(),
                               markdown::SetHeadingLevel{*node, level});
      }
    });
  }
  const QStringList inlineNames{
      text(QStringLiteral("action.bold"), QStringLiteral("&Bold")),
      text(QStringLiteral("action.italic"), QStringLiteral("&Italic")),
      text(QStringLiteral("action.strikethrough"),
           QStringLiteral("&Strikethrough"))};
  const QStringList inlineIds{QStringLiteral("boldAction"),
                              QStringLiteral("italicAction"),
                              QStringLiteral("strikethroughAction")};
  for (int index = 0; index < inlineNames.size(); ++index) {
    auto* value = format->addAction(inlineNames[index]);
    value->setObjectName(inlineIds[index]);
    value->setCheckable(true);
    const auto style = static_cast<markdown::InlineStyle>(index);
    if (style == markdown::InlineStyle::kBold) {
      value->setShortcut(QKeySequence::Bold);
    } else if (style == markdown::InlineStyle::kItalic) {
      value->setShortcut(QKeySequence::Italic);
    }
    inlineActions_.push_back(value);
    connect(value, &QAction::triggered, this,
            [this, style] { continuous_->toggleInlineStyle(style); });
  }
  const auto unavailable = [&](const QString& label, const QString& name) {
    auto* value = format->addAction(label);
    value->setObjectName(name);
    value->setEnabled(false);
    value->setToolTip(QStringLiteral("Not implemented in WYSIWYG yet; use "
                                     "Markdown mode"));
    return value;
  };
  blockquoteAction_ = format->addAction(
      text(QStringLiteral("action.blockquote"), QStringLiteral("Blockquote")));
  blockquoteAction_->setObjectName(QStringLiteral("blockquoteAction"));
  connect(blockquoteAction_, &QAction::triggered, this,
          [this] { continuous_->toggleBlockquote(); });
  auto* blockquote = blockquoteAction_;
  auto* code =
      unavailable(text(QStringLiteral("action.code"), QStringLiteral("Code")),
                  QStringLiteral("codeAction"));
  auto* horizontal = unavailable(
      text(QStringLiteral("action.horizontalLine"),
           QStringLiteral("Horizontal Line")),
      QStringLiteral("horizontalLineAction"));
  auto* link =
      unavailable(text(QStringLiteral("action.link"), QStringLiteral("Link")),
                  QStringLiteral("linkAction"));
  auto* image =
      unavailable(text(QStringLiteral("action.image"), QStringLiteral("Image")),
                  QStringLiteral("imageAction"));
  tableAction_ = format->addAction(
      text(QStringLiteral("action.table"), QStringLiteral("Table")));
  tableAction_->setObjectName(QStringLiteral("tableAction"));
  connect(tableAction_, &QAction::triggered, this,
          [this] { continuous_->insertTable(); });
  auto* table = tableAction_;
  auto* listMenu = format->addMenu(
      text(QStringLiteral("action.list"), QStringLiteral("List")));
  listMenu->setObjectName(QStringLiteral("listMenu"));
  listMenu->menuAction()->setObjectName(QStringLiteral("listMenuAction"));
  orderedListAction_ = listMenu->addAction(
      text(QStringLiteral("action.orderedList"), QStringLiteral("Ordered")));
  orderedListAction_->setObjectName(QStringLiteral("orderedListAction"));
  connect(orderedListAction_, &QAction::triggered, this,
          [this] { continuous_->changeListKind(true); });
  auto* ordered = orderedListAction_;
  unorderedListAction_ = listMenu->addAction(
      text(QStringLiteral("action.unorderedList"), QStringLiteral("Unordered")));
  unorderedListAction_->setObjectName(QStringLiteral("unorderedListAction"));
  connect(unorderedListAction_, &QAction::triggered, this,
          [this] { continuous_->changeListKind(false); });
  auto* unordered = unorderedListAction_;
  taskAction_ = new QAction(
      text(QStringLiteral("action.toggleTask"),
           QStringLiteral("Toggle task checked")),
      this);
  taskAction_->setObjectName(QStringLiteral("toggleTaskAction"));
  connect(taskAction_, &QAction::triggered, this, [this] {
    const auto node = continuous_->taskNode();
    if (node) {
      applyContinuousCommand(
          continuous_->token(),
          markdown::SetTaskChecked{*node, !continuous_->taskChecked()});
    }
  });
  tableMenu_ = new QMenu(text(QStringLiteral("action.table"),
                              QStringLiteral("Table")),
                        this);
  tableMenu_->setObjectName(QStringLiteral("tableContextMenu"));
  tableMenu_->menuAction()->setObjectName(
      QStringLiteral("tableContextMenuAction"));
  const auto addTableAction = [this](const QString& label, const QString& name,
                                     ContinuousEditor::TableAction kind) {
    auto* value = tableMenu_->addAction(label);
    value->setObjectName(name);
    connect(value, &QAction::triggered, this,
            [this, kind] { continuous_->applyTableAction(kind); });
    return value;
  };
  addTableAction(QStringLiteral("Insert Row Above"),
                 QStringLiteral("tableInsertRowAboveAction"),
                 ContinuousEditor::TableAction::kInsertRowAbove);
  addTableAction(QStringLiteral("Insert Row Below"),
                 QStringLiteral("tableInsertRowBelowAction"),
                 ContinuousEditor::TableAction::kInsertRowBelow);
  addTableAction(QStringLiteral("Delete Row"),
                 QStringLiteral("tableRemoveRowAction"),
                 ContinuousEditor::TableAction::kRemoveRow);
  tableMenu_->addSeparator();
  addTableAction(QStringLiteral("Insert Column Left"),
                 QStringLiteral("tableInsertColumnLeftAction"),
                 ContinuousEditor::TableAction::kInsertColumnLeft);
  addTableAction(QStringLiteral("Insert Column Right"),
                 QStringLiteral("tableInsertColumnRightAction"),
                 ContinuousEditor::TableAction::kInsertColumnRight);
  addTableAction(QStringLiteral("Delete Column"),
                 QStringLiteral("tableRemoveColumnAction"),
                 ContinuousEditor::TableAction::kRemoveColumn);
  tableMenu_->addSeparator();
  addTableAction(QStringLiteral("Align Column Left"),
                 QStringLiteral("tableAlignLeftAction"),
                 ContinuousEditor::TableAction::kAlignLeft);
  addTableAction(QStringLiteral("Align Column Center"),
                 QStringLiteral("tableAlignCenterAction"),
                 ContinuousEditor::TableAction::kAlignCenter);
  addTableAction(QStringLiteral("Align Column Right"),
                 QStringLiteral("tableAlignRightAction"),
                 ContinuousEditor::TableAction::kAlignRight);
  tableMenu_->addSeparator();
  addTableAction(QStringLiteral("Delete Table"),
                 QStringLiteral("tableRemoveTableAction"),
                 ContinuousEditor::TableAction::kRemoveTable);
  auto* view = menuBar()->addMenu(
      text(QStringLiteral("menu.view"), QStringLiteral("&View")));
  view->setObjectName(QStringLiteral("viewMenu"));
  auto* fullScreen = view->addAction(
      text(QStringLiteral("view.fullScreen"), QStringLiteral("Full Screen")));
  fullScreen->setObjectName(QStringLiteral("fullScreenAction"));
  fullScreen->setCheckable(true);
  connect(fullScreen, &QAction::toggled, this, [this](bool enabled) {
    if (enabled) {
      showFullScreen();
    } else {
      showNormal();
    }
  });
  const auto setZoom = [this](double value) {
    zoom_ = std::clamp(value, 0.5, 4.0);
    QString error;
    if (!applySettings(settings_, &error)) {
      dialogs_.showError(this, error);
    }
    refreshState();
  };
  auto* zoomIn = view->addAction(
      text(QStringLiteral("view.zoomIn"), QStringLiteral("Zoom In")));
  zoomIn->setObjectName(QStringLiteral("zoomInAction"));
  zoomIn->setShortcut(QKeySequence::ZoomIn);
  connect(zoomIn, &QAction::triggered, this, [setZoom, this] {
    setZoom(zoom_ * 1.1);
  });
  auto* zoomOut = view->addAction(
      text(QStringLiteral("view.zoomOut"), QStringLiteral("Zoom Out")));
  zoomOut->setObjectName(QStringLiteral("zoomOutAction"));
  zoomOut->setShortcut(QKeySequence::ZoomOut);
  connect(zoomOut, &QAction::triggered, this, [setZoom, this] {
    setZoom(zoom_ / 1.1);
  });
  auto* resetZoom = view->addAction(
      text(QStringLiteral("view.resetZoom"), QStringLiteral("Reset Zoom")));
  resetZoom->setObjectName(QStringLiteral("resetZoomAction"));
  resetZoom->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
  connect(resetZoom, &QAction::triggered, this, [setZoom] { setZoom(1.0); });
  view->addSeparator();
  auto* modes = view->addMenu(
      text(QStringLiteral("view.switchMode"), QStringLiteral("Switch Mode")));
  modes->setObjectName(QStringLiteral("switchModeMenu"));
  modes->menuAction()->setObjectName(QStringLiteral("switchModeAction"));
  markdownAction_ = modes->addAction(
      text(QStringLiteral("view.markdown"), QStringLiteral("Markdown")));
  markdownAction_->setObjectName(QStringLiteral("markdownModeAction"));
  markdownAction_->setCheckable(true);
  continuousAction_ = modes->addAction(
      text(QStringLiteral("view.continuous"),
           QStringLiteral("WYSIWYG (Continuous layout)")));
  continuousAction_->setObjectName(QStringLiteral("continuousModeAction"));
  continuousAction_->setCheckable(true);
  pagedAction_ = modes->addAction(
      text(QStringLiteral("view.paged"),
           QStringLiteral("WYSIWYG (Page layout)")));
  pagedAction_->setObjectName(QStringLiteral("pagedModeAction"));
  pagedAction_->setCheckable(true);
  connect(markdownAction_, &QAction::triggered, this,
          [this] { setContinuousMode(false); });
  connect(continuousAction_, &QAction::triggered, this, [this] {
    setPagedMode(false);
    setContinuousMode(true);
  });
  connect(pagedAction_, &QAction::triggered, this,
          [this] { setPagedMode(true); });
  auto* help = menuBar()->addMenu(
      text(QStringLiteral("menu.help"), QStringLiteral("&Help")));
  help->setObjectName(QStringLiteral("helpMenu"));
  const QList<QAction*> contextActions{
      undoAction_, redoAction_, cutAction_, copyAction_, pasteAction,
      selectAllAction, headingActions_.front(), taskAction_};
  for (auto* target : {static_cast<QWidget*>(editor_),
                       static_cast<QWidget*>(continuous_->editor())}) {
    for (auto* contextAction : contextActions) {
      target->addAction(contextAction);
    }
    for (auto* inlineAction : inlineActions_) {
      target->addAction(inlineAction);
    }
    target->setContextMenuPolicy(Qt::ActionsContextMenu);
  }
  continuous_->editor()->addAction(tableMenu_->menuAction());
  toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
  toolbar->setIconSize(QSize(20, 20));
  toolbar->addSeparator();
  toolbar->addAction(undoAction_);
  toolbar->addAction(redoAction_);
  toolbar->addAction(settingsAction);
  toolbar->addSeparator();
  toolbar->addAction(headings->menuAction());
  for (auto* inlineAction : inlineActions_) {
    toolbar->addAction(inlineAction);
  }
  toolbar->addAction(blockquote);
  toolbar->addAction(code);
  toolbar->addAction(horizontal);
  toolbar->addAction(link);
  toolbar->addAction(image);
  toolbar->addAction(table);
  toolbar->addAction(listMenu->menuAction());
  toolbar->addAction(ordered);
  toolbar->addAction(unordered);
  toolbar->addSeparator();
  toolbar->addAction(modes->menuAction());
  refreshToolbarIcons();
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
  connect(continuous_->editor(), &QTextEdit::cursorPositionChanged, this,
          &MainWindow::refreshState);
  connect(continuous_->editor(), &QTextEdit::selectionChanged, this,
          &MainWindow::refreshState);
  connect(continuous_, &ContinuousEditor::errorOccurred, this,
          [this](const QString& error) { dialogs_.showError(this, error); });
  connect(continuous_, &ContinuousEditor::commandRequested, this,
          &MainWindow::applyContinuousCommand);
  refreshEditor();
  QString settingsError;
  if (!applySettings(settings_, &settingsError)) {
    settingsDiagnostic_ += settingsError;
  }
  if (!settingsDiagnostic_.isEmpty()) {
    QTimer::singleShot(0, this, [this] {
      statusBar()->showMessage(settingsDiagnostic_, 10000);
    });
  }
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

bool MainWindow::exportPdf() {
  const auto snapshot = session_.snapshot();
  auto parsed = markdown::parse(
      QByteArray(snapshot.source.data(), qsizetype(snapshot.source.size())));
  if (!parsed.document) {
    dialogs_.showError(this, parsed.error);
    return false;
  }
  QString suggested = displayPath(session_.path());
  if (suggested.isEmpty()) {
    suggested = QStringLiteral("document");
  }
  const QFileInfo info(suggested);
  suggested =
      QDir(info.path()).filePath(info.completeBaseName() + QStringLiteral(".pdf"));
  const auto selected = QFileDialog::getSaveFileName(
      this, QStringLiteral("Export PDF"), suggested,
      QStringLiteral("PDF (*.pdf);;All files (*)"));
  if (selected.isEmpty()) {
    return false;
  }
  PdfExportOptions options;
  options.page = settings_.page;
  options.fontFamily = settings_.editorFontFamily;
  options.fontSize = settings_.editorFontSize;
  const auto result =
      markpad::exportPdf(*parsed.document, filePath(selected), options);
  if (!result.success()) {
    dialogs_.showError(this, *result.error);
    return false;
  }
  if (!result.warning.isEmpty()) {
    statusBar()->showMessage(result.warning, 10000);
  } else {
    statusBar()->showMessage(
        QStringLiteral("PDF exported (%1 pages)").arg(result.pageCount), 3000);
  }
  return true;
}

bool MainWindow::printDocument() {
  const auto snapshot = session_.snapshot();
  auto parsed = markdown::parse(
      QByteArray(snapshot.source.data(), qsizetype(snapshot.source.size())));
  if (!parsed.document) {
    dialogs_.showError(this, parsed.error);
    return false;
  }
  QPrinter printer(QPrinter::HighResolution);
  const auto margins = QMarginsF(
      settings_.page.marginLeftMm, settings_.page.marginTopMm,
      settings_.page.marginRightMm, settings_.page.marginBottomMm);
  printer.setPageLayout(QPageLayout(
      settings_.page.size == PageSize::kLetter
          ? QPageSize(QPageSize::Letter)
          : settings_.page.size == PageSize::kLegal
                ? QPageSize(QPageSize::Legal)
                : QPageSize(QPageSize::A4),
      settings_.page.orientation == PageOrientation::kLandscape
          ? QPageLayout::Landscape
          : QPageLayout::Portrait,
      margins, QPageLayout::Millimeter));
  QPrintDialog dialog(&printer, this);
  dialog.setWindowTitle(QStringLiteral("Print"));
  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }
  PdfExportOptions options;
  options.page = settings_.page;
  options.fontFamily = settings_.editorFontFamily;
  options.fontSize = settings_.editorFontSize;
  if (const auto error =
          markpad::printDocument(*parsed.document, printer, options)) {
    dialogs_.showError(this, *error);
    return false;
  }
  return true;
}

bool MainWindow::exportHtml() {
  const auto snapshot = session_.snapshot();
  auto parsed = markdown::parse(
      QByteArray(snapshot.source.data(), qsizetype(snapshot.source.size())));
  if (!parsed.document) {
    dialogs_.showError(this, parsed.error);
    return false;
  }
  const auto rendered = markdown::renderHtml(*parsed.document);
  if (!rendered.html) {
    dialogs_.showError(this, rendered.error);
    return false;
  }
  QString suggested = displayPath(session_.path());
  if (suggested.isEmpty()) {
    suggested = QStringLiteral("document");
  }
  const QFileInfo info(suggested);
  suggested =
      QDir(info.path()).filePath(info.completeBaseName() + QStringLiteral(".html"));
  const auto selected = QFileDialog::getSaveFileName(
      this, QStringLiteral("Export HTML"), suggested,
      QStringLiteral("HTML (*.html *.htm);;All files (*)"));
  if (selected.isEmpty()) {
    return false;
  }
  if (const auto error = writeHtmlFile(filePath(selected), *rendered.html)) {
    dialogs_.showError(this, *error);
    return false;
  }
  statusBar()->showMessage(QStringLiteral("HTML exported"), 3000);
  return true;
}

QString MainWindow::text(const QString& key, const QString& fallback) const {
  return languageText_.value(key, fallback);
}

bool MainWindow::applySettings(const Settings& settings, QString* error) {
  QPalette palette = QApplication::style()->standardPalette();
  if (settings.theme == Theme::kDark) {
    const QColor window(45, 45, 48);
    const QColor base(30, 30, 32);
    const QColor text(230, 230, 230);
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, window);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, window);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::ToolTipBase, base);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Highlight, QColor(68, 112, 180));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Disabled, QPalette::Text,
                     QColor(140, 140, 140));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                     QColor(140, 140, 140));
  }
  QApplication::setPalette(palette);
  refreshToolbarIcons();

  QFont font = settings.editorFontFamily.trimmed().isEmpty()
                   ? QFontDatabase::systemFont(QFontDatabase::FixedFont)
                   : QFont(settings.editorFontFamily);
  font.setPointSize(
      std::max(1, qRound(settings.editorFontSize * zoom_)));
  editor_->setFont(font);
  continuous_->editor()->setFont(font);
  editor_->setLineWrapMode(settings.wordWrap ? QPlainTextEdit::WidgetWidth
                                             : QPlainTextEdit::NoWrap);
  continuous_->editor()->setLineWrapMode(
      settings.wordWrap ? QTextEdit::WidgetWidth : QTextEdit::NoWrap);
  dialogs_.setDefaultLocation(settings.defaultFileLocation);

  QMap<QString, QAction*> actions;
  for (auto* action : findChildren<QAction*>()) {
    if (!action->objectName().isEmpty()) {
      actions.insert(action->objectName(), action);
    }
  }
  QStringList unknown;
  for (auto it = settings.shortcuts.begin(); it != settings.shortcuts.end();
       ++it) {
    auto* action = actions.value(it.key());
    if (!action) {
      unknown.push_back(it.key());
      continue;
    }
    action->setShortcut(QKeySequence::fromString(it.value()));
  }
  if (!unknown.isEmpty() && error) {
    *error = QStringLiteral("Unknown shortcut targets: %1")
                 .arg(unknown.join(QStringLiteral(", ")));
    return false;
  }
  continuous_->setPageSetup(settings.page);
  if (pagedMode_) {
    continuous_->setPaged(true);
  }
  return true;
}

void MainWindow::showSettings() {
  SettingsDialog dialog(settingsService_.languages(), this);
  dialog.setValues(settings_);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const auto values = dialog.values();
  if (const auto error = settingsService_.save(values)) {
    dialogs_.showError(this, *error);
    return;
  }
  const auto previousLanguage = settings_.language;
  settings_ = values;
  languageText_ = settingsService_.languageMap(settings_.language);
  QString error;
  if (!applySettings(settings_, &error)) {
    dialogs_.showError(this, error);
  }
  if (previousLanguage != settings_.language) {
    statusBar()->showMessage(
        QStringLiteral("Language changes take effect after restart"), 5000);
  } else {
    statusBar()->showMessage(QStringLiteral("Preferences saved"), 3000);
  }
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
  if (continuousMode_) {
    const auto error = continuous_->render(snapshot);
    if (!error.isEmpty()) {
      dialogs_.showError(this, error);
    }
    const auto raw = sourceText(snapshot.source);
    const auto projection = project(raw);
    const auto offset = projection.offsets[size_t(cursor.position())];
    bool exact = false;
    const int richPosition =
        continuous_->viewPosition(raw.left(offset).toUtf8().size(), &exact);
    auto richCursor = continuous_->editor()->textCursor();
    richCursor.setPosition(richPosition);
    continuous_->editor()->setTextCursor(richCursor);
    continuous_->explainCursorMapping(exact);
  }
  refreshState();
}

void MainWindow::refreshToolbarIcons() {
  auto* toolbar = findChild<QToolBar*>(QStringLiteral("mainToolBar"));
  if (!toolbar) {
    return;
  }
  const auto color = QApplication::palette().color(QPalette::ButtonText);
  for (auto* action : toolbar->actions()) {
    if (!action || action->isSeparator() || action->objectName().isEmpty()) {
      continue;
    }
    const auto icon = toolbarIcon(action->objectName(), color);
    if (!icon.isNull()) {
      action->setIcon(icon);
    }
    if (action->toolTip().isEmpty()) {
      action->setToolTip(action->text());
    }
  }
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
  const auto cursor = continuousMode_ ? continuous_->editor()->textCursor()
                                      : editor_->textCursor();
  cutAction_->setEnabled(cursor.hasSelection());
  copyAction_->setEnabled(cursor.hasSelection());
  markdownAction_->setChecked(!continuousMode_);
  continuousAction_->setChecked(continuousMode_ && !pagedMode_);
  pagedAction_->setChecked(continuousMode_ && pagedMode_);
  for (auto* action : headingActions_) {
    action->setEnabled(continuousMode_ &&
                       continuous_->headingNode().has_value());
  }
  taskAction_->setEnabled(continuousMode_ &&
                          continuous_->taskNode().has_value());
  for (size_t index = 0; index < inlineActions_.size(); ++index) {
    const auto state = continuousMode_
                           ? continuous_->inlineState(
                                 static_cast<markdown::InlineStyle>(index))
                           : ContinuousEditor::InlineState{};
    auto* action = inlineActions_[index];
    action->setEnabled(continuousMode_ && state.command.has_value());
    action->setChecked(continuousMode_ && state.checked);
    action->setToolTip(state.reason);
  }
  const auto quoteState =
      continuousMode_ ? continuous_->blockquoteState()
                      : ContinuousEditor::StructuralState{};
  blockquoteAction_->setEnabled(continuousMode_ &&
                                quoteState.command.has_value());
  blockquoteAction_->setToolTip(quoteState.reason);
  const auto orderedState = continuousMode_
                                ? continuous_->listState(true)
                                : ContinuousEditor::StructuralState{};
  orderedListAction_->setEnabled(continuousMode_ &&
                                 orderedState.command.has_value());
  orderedListAction_->setToolTip(orderedState.reason);
  const auto unorderedState = continuousMode_
                                  ? continuous_->listState(false)
                                  : ContinuousEditor::StructuralState{};
  unorderedListAction_->setEnabled(continuousMode_ &&
                                   unorderedState.command.has_value());
  unorderedListAction_->setToolTip(unorderedState.reason);
  tableAction_->setEnabled(continuousMode_ &&
                           continuous_->tableCreationEnabled());
  tableAction_->setToolTip(
      continuousMode_
          ? QStringLiteral("Insert a 2-column table after the current block")
          : QStringLiteral("Table creation requires continuous WYSIWYG mode"));
  if (tableMenu_) {
    tableMenu_->menuAction()->setEnabled(continuousMode_);
    for (auto* tableAction : tableMenu_->actions()) {
      if (!tableAction || tableAction->isSeparator()) {
        continue;
      }
      const auto kind = tableActionKind(tableAction->objectName());
      tableAction->setEnabled(continuousMode_ && kind.has_value() &&
                              continuous_->tableActionEnabled(*kind));
    }
  }
  const auto documentText = sourceText(session_.snapshot().source);
  int words = 0;
  bool inWord = false;
  for (const auto character : documentText) {
    if (character.isSpace()) {
      inWord = false;
    } else if (!inWord) {
      ++words;
      inWord = true;
    }
  }
  const int lines =
      documentText.isEmpty() ? 0 : int(documentText.count(QChar('\n')) + 1);
  QString status =
      QStringLiteral("Chars %1 | Words %2 | Lines %3")
          .arg(documentText.size())
          .arg(words)
          .arg(lines);
  if (pagedMode_) {
    status += QStringLiteral(" | Page %1 of %2")
                  .arg(continuous_->currentPage() + 1)
                  .arg(std::max(1, continuous_->pageCount()));
    if (!continuous_->layoutWarning().isEmpty()) {
      status += QStringLiteral(" | Overflow warning");
    }
  }
  statusBar()->showMessage(status);
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

bool MainWindow::setContinuousMode(bool enabled) {
  if (continuousMode_ == enabled) {
    refreshState();
    return true;
  }
  if (!enabled) {
    pagedMode_ = false;
    continuous_->setPaged(false);
  }
  const auto snapshot = session_.snapshot();
  const auto raw = sourceText(snapshot.source);
  const auto projection = project(raw);
  bool exact = true;
  if (enabled) {
    const auto error = continuous_->render(snapshot);
    if (!error.isEmpty()) {
      dialogs_.showError(this, error);
      refreshState();
      return false;
    }
    const auto sourceCursor = editor_->textCursor();
    const auto map = [&](int position) {
      bool mapped = false;
      const auto offset = projection.offsets[size_t(position)];
      const auto result =
          continuous_->viewPosition(raw.left(offset).toUtf8().size(), &mapped);
      exact = exact && mapped;
      return result;
    };
    auto cursor = continuous_->editor()->textCursor();
    cursor.setPosition(map(sourceCursor.anchor()));
    cursor.setPosition(map(sourceCursor.position()), QTextCursor::KeepAnchor);
    continuous_->editor()->setTextCursor(cursor);
    continuous_->explainCursorMapping(exact);
    modeSelection_ = ModeSelection{snapshot.token, sourceCursor.anchor(),
                                   sourceCursor.position(), cursor.anchor(),
                                   cursor.position()};
  } else {
    const auto richCursor = continuous_->editor()->textCursor();
    const auto anchor = continuous_->sourcePosition(
        richCursor.anchor(), richCursor.anchor() <= richCursor.position());
    const auto position = continuous_->sourcePosition(
        richCursor.position(), richCursor.position() <= richCursor.anchor());
    if (modeSelection_ && modeSelection_->token == snapshot.token &&
        modeSelection_->viewAnchor == richCursor.anchor() &&
        modeSelection_->viewPosition == richCursor.position()) {
      auto cursor = editor_->textCursor();
      cursor.setPosition(modeSelection_->sourceAnchor);
      cursor.setPosition(modeSelection_->sourcePosition,
                         QTextCursor::KeepAnchor);
      editor_->setTextCursor(cursor);
    } else if (anchor && position) {
      const auto map = [&](qsizetype offset) {
        return int(
            project(sourceText(snapshot.source.substr(0, size_t(offset))))
                .text.size());
      };
      auto cursor = editor_->textCursor();
      cursor.setPosition(map(*anchor));
      cursor.setPosition(map(*position), QTextCursor::KeepAnchor);
      editor_->setTextCursor(cursor);
    } else {
      exact = false;
    }
    modeSelection_.reset();
  }
  continuousMode_ = enabled;
  workspace_->setCurrentWidget(enabled ? static_cast<QWidget*>(continuous_)
                                       : static_cast<QWidget*>(editor_));
  if (enabled) {
    continuous_->editor()->setFocus();
  } else {
    editor_->setFocus();
  }
  refreshState();
  if (!exact && !enabled) {
    statusBar()->showMessage(
        QStringLiteral("Markdown | Cursor/selection retained: protected "
                       "fragment has no exact source mapping."));
  }
  return true;
}

bool MainWindow::setPagedMode(bool enabled) {
  if (enabled) {
    if (!continuousMode_ && !setContinuousMode(true)) {
      return false;
    }
    pagedMode_ = true;
  } else {
    pagedMode_ = false;
  }
  continuous_->setPaged(enabled);
  continuous_->setPageSetup(settings_.page);
  refreshState();
  if (enabled && !continuous_->layoutWarning().isEmpty()) {
    statusBar()->showMessage(continuous_->layoutWarning(), 10000);
  }
  if (continuousMode_) {
    continuous_->editor()->setFocus();
  }
  return true;
}

bool MainWindow::applyContinuousCommand(const DocumentToken& token,
                                        const markdown::EditCommand& command,
                                        int cursor) {
  const auto before = session_.snapshot();
  const auto fail = [&](const QString& error) {
    continuous_->render(before);
    dialogs_.showError(this, error);
    refreshState();
    return false;
  };
  if (!continuousMode_ || token != before.token ||
      token != continuous_->token()) {
    return fail(QStringLiteral("Stale or foreign WYSIWYG snapshot"));
  }
  const auto result = continuous_->apply(command);
  if (!result.document) {
    return fail(result.error);
  }
  if (cursor < 0) {
    cursor = continuous_->editor()->textCursor().position();
  }
  const auto after = result.document->source.toStdString();
  if (after == before.source) {
    continuous_->render(before);
    auto richCursor = continuous_->editor()->textCursor();
    richCursor.setPosition(std::clamp(
        cursor, 0, continuous_->editor()->document()->characterCount() - 1));
    continuous_->editor()->setTextCursor(richCursor);
    refreshState();
    return true;
  }
  const int beforeCursor = editor_->textCursor().position();
  const int selectionAnchor = continuous_->editor()->textCursor().anchor();
  const int selectionPosition = continuous_->editor()->textCursor().position();
  const bool preserveSelection =
      std::holds_alternative<markdown::ToggleInlineStyle>(command);
  if (!applySource(after, beforeCursor)) {
    return false;
  }
  auto richCursor = continuous_->editor()->textCursor();
  richCursor.setPosition(std::clamp(
      cursor, 0, continuous_->editor()->document()->characterCount() - 1));
  if (preserveSelection) {
    richCursor.setPosition(selectionAnchor);
    richCursor.setPosition(selectionPosition, QTextCursor::KeepAnchor);
  }
  continuous_->editor()->setTextCursor(richCursor);
  if (const auto offset = continuous_->sourcePosition(richCursor.position())) {
    const int position =
        int(project(sourceText(after.substr(0, size_t(*offset)))).text.size());
    auto sourceCursor = editor_->textCursor();
    sourceCursor.setPosition(position);
    editor_->setTextCursor(sourceCursor);
  }
  history_.resize(historyPosition_);
  history_.push_back(
      {before.source, after, beforeCursor, editor_->textCursor().position()});
  historyPosition_ = history_.size();
  refreshState();
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
  if ((watched == editor_ || watched == continuous_->editor()) &&
      (event->type() == QEvent::ShortcutOverride ||
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
