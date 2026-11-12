#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileOpenEvent>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextFormat>
#include <QTextLayout>
#include <QTextTable>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QtTest/QtTest>
#include <algorithm>

#include "application_controller.h"
#include "continuous_editor.h"
#include "settings_dialog.h"

namespace markpad {

class RecordedUrlHandlers final : public QObject {
  Q_OBJECT

 public:
  RecordedUrlHandlers() {
    for (const auto& scheme : schemes_) {
      QDesktopServices::setUrlHandler(scheme, this, "record");
    }
  }
  ~RecordedUrlHandlers() override {
    for (const auto& scheme : schemes_) {
      QDesktopServices::unsetUrlHandler(scheme);
    }
  }
  QList<QUrl> urls;

 public slots:
  void record(const QUrl& url) { urls.append(url); }

 private:
  const QStringList schemes_{"http", "https",      "mailto",
                             "file", "javascript", "data"};
};

namespace {

class TestDialogs final : public DocumentDialogs {
 public:
  QString openPath;
  std::optional<SaveSelection> saveSelection;
  std::optional<Encoding> encoding;
  std::optional<QString> linkUrl;
  std::optional<QString> imageUrl;
  std::optional<TableSelection> tableSelection = TableSelection{2, 1};
  CloseChoice closeChoice = CloseChoice::kCancel;
  QStringList errors;
  int closePrompts = 0;
  int linkPrompts = 0;
  int imagePrompts = 0;
  int tablePrompts = 0;
  QString chooseOpen(QWidget*) override { return openPath; }
  std::optional<SaveSelection> chooseSave(QWidget*, const QString&,
                                          FileFormat) override {
    return saveSelection;
  }
  std::optional<Encoding> chooseEncoding(QWidget*) override { return encoding; }
  std::optional<QString> chooseLink(QWidget*) override {
    ++linkPrompts;
    return linkUrl;
  }
  std::optional<QString> chooseImage(QWidget*) override {
    ++imagePrompts;
    return imageUrl;
  }
  std::optional<TableSelection> chooseTableSize(QWidget*) override {
    ++tablePrompts;
    return tableSelection;
  }
  CloseChoice confirmClose(QWidget*, const QString&) override {
    ++closePrompts;
    return closeChoice;
  }
  void showError(QWidget*, const QString& message) override {
    errors.append(message);
  }
};

bool put(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray get(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

QAction* action(MainWindow* window, const char* name) {
  return window->findChild<QAction*>(QString::fromLatin1(name));
}

}  // namespace

class MainWindowSmokeTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { QApplication::setQuitOnLastWindowClosed(false); }

  void newAndOpenPolicy() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto firstPath = dir.filePath("first.md");
    const auto secondPath = dir.filePath("second.md");
    QVERIFY(put(firstPath, "# first\r\n"));
    QVERIFY(put(secondPath, "second\n"));
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* first = controller.newDocument();
    QVERIFY(first->isVisible());
    QVERIFY(first->editor());
    QVERIFY(first->reusable());
    QCOMPARE(controller.openDocument(firstPath, first), first);
    QCOMPARE(controller.windows().size(), 1);
    QCOMPARE(first->session().snapshot().source, std::string("# first\r\n"));
    QVERIFY(first->windowTitle().contains("first.md"));
    QVERIFY(!first->isWindowModified());
    auto* second = controller.openDocument(secondPath, first);
    QVERIFY(second && second != first);
    QCOMPARE(controller.windows().size(), 2);
    second->editor()->insertPlainText("edit ");
    QVERIFY(second->session().dirty());
    QVERIFY(second->isWindowModified());
    QCOMPARE(first->session().snapshot().source, std::string("# first\r\n"));
    action(first, "newAction")->trigger();
    QCOMPARE(controller.windows().size(), 3);
    QVERIFY(controller.windows().back()->reusable());
    dialogs.openPath = secondPath;
    auto* empty = controller.windows().back();
    action(empty, "openAction")->trigger();
    QCOMPARE(controller.windows().size(), 3);
    QCOMPARE(empty->session().snapshot().source, std::string("second\n"));
    auto* dirtyEmpty = controller.newDocument();
    dirtyEmpty->editor()->insertPlainText("unsaved");
    auto* separate = controller.openDocument(firstPath, dirtyEmpty);
    QVERIFY(separate != dirtyEmpty);
    QCOMPARE(dirtyEmpty->session().snapshot().source, std::string("unsaved"));
    QVERIFY(dialogs.errors.empty());
  }

  void failedOpenDoesNotLoseDocuments() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    const auto token = window->session().snapshot().token;
    QVERIFY(!controller.openDocument(dir.filePath("missing.md"), window));
    QCOMPARE(controller.windows().size(), 1);
    QCOMPARE(window->session().snapshot().token, token);
    QVERIFY(!dialogs.errors.empty());
    window->editor()->insertPlainText("keep");
    QVERIFY(!controller.openDocument(dir.filePath("missing.md"), window));
    QCOMPARE(controller.windows().size(), 1);
    QCOMPARE(window->session().snapshot().source, std::string("keep"));
    const auto invalid = dir.filePath("invalid.md");
    QVERIFY(put(invalid, QByteArray::fromHex("fffe00d8")));
    QVERIFY(!controller.openDocument(invalid, window));
    QCOMPARE(controller.windows().size(), 1);
    const auto explicitFile = dir.filePath("utf16.md");
    QVERIFY(put(explicitFile, QByteArray::fromHex("9f04a004")));
    dialogs.encoding = Encoding::kUtf16Le;
    auto* decoded = controller.openDocument(explicitFile, window);
    QVERIFY(decoded);
    QCOMPARE(decoded->session().snapshot().source,
             QByteArray::fromHex("d29fd2a0").toStdString());
    dialogs.openPath.clear();
    const auto count = controller.windows().size();
    action(window, "openAction")->trigger();
    QCOMPARE(controller.windows().size(), count);
  }

  void sourcePreservation_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::newRow("mixed-eol") << QByteArray("one\r\ntwo\rthree\n");
    QTest::newRow("utf8")
        << QStringLiteral(
               "# \u041f\u0440\u0438\u0432\u0435\u0442 \U0001f600\r\n"
               "a\u00a0b\u2028c\u2029d\n")
               .toUtf8();
    QTest::newRow("literal-bom")
        << QByteArray::fromHex("efbbbfefbbbf41") + "\r\nB";
    QTest::newRow("raw-html")
        << QByteArray("<script>alert('x')</script>\n\n[a](file:///missing)\n");
  }

  void sourcePreservation() {
    QFETCH(QByteArray, source);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("source.md");
    QVERIFY(put(path, source));
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.openDocument(path);
    QVERIFY(window);
    const auto original = window->session().snapshot();
    QVERIFY(window->saveDocument());
    QCOMPARE(get(path), source);
    auto cursor = window->editor()->textCursor();
    cursor.movePosition(QTextCursor::End);
    window->editor()->setTextCursor(cursor);
    window->editor()->insertPlainText("x");
    QCOMPARE(window->session().snapshot().source, original.source + "x");
    QVERIFY(window->session().dirty());
    action(window, "undoAction")->trigger();
    QCOMPARE(window->session().snapshot().source, original.source);
    QVERIFY(!window->session().dirty());
    action(window, "redoAction")->trigger();
    QCOMPARE(window->session().snapshot().source, original.source + "x");
    QVERIFY(window->saveDocument());
    QCOMPARE(get(path), source + "x");
    window->undo();
    QVERIFY(window->session().dirty());
    window->redo();
    QVERIFY(!window->session().dirty());
    QVERIFY(dialogs.errors.empty());
  }

  void lineBreaksAndUndo() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("lines.md");
    const QByteArray original = "one\r\ntwo\rthree\n";
    QVERIFY(put(path, original));
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.openDocument(path);
    QVERIFY(window);
    auto cursor = window->editor()->textCursor();
    cursor.setPosition(4);
    cursor.setPosition(8, QTextCursor::KeepAnchor);
    window->editor()->setTextCursor(cursor);
    window->editor()->insertPlainText("new\n");
    QCOMPARE(window->session().snapshot().source,
             std::string("one\r\nnew\r\nthree\n"));
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.toStdString());
    window->redo();
    window->undo();
    window->editor()->insertPlainText("branch");
    QVERIFY(!action(window, "redoAction")->isEnabled());
    window->editor()->selectAll();
    window->editor()->insertPlainText(QString(QChar(0)));
    QVERIFY(!dialogs.errors.empty());
    QVERIFY(validDocumentText(window->session().snapshot().source));
  }

  void keyboardUndoRedo() {
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->activateWindow();
    window->editor()->setFocus();
    QCoreApplication::processEvents();
    QTest::keyClicks(window->editor(), "hello");
    QCOMPARE(window->session().snapshot().source, std::string("hello"));
    QTest::keySequence(window->editor(), QKeySequence(QKeySequence::Undo));
    QCOMPARE(window->session().snapshot().source, std::string("hell"));
    QTest::keySequence(window->editor(), QKeySequence(QKeySequence::Redo));
    QCOMPARE(window->session().snapshot().source, std::string("hello"));
    QVERIFY(dialogs.errors.empty());
  }

  void unicodeReplacement() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("unicode.md");
    const auto source =
        QStringLiteral("a\U0001f600\u00a0b\r\nc\u2029d").toUtf8();
    QVERIFY(put(path, source));
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.openDocument(path);
    QVERIFY(window);
    auto cursor = window->editor()->textCursor();
    cursor.setPosition(1);
    cursor.setPosition(4, QTextCursor::KeepAnchor);
    window->editor()->setTextCursor(cursor);
    window->editor()->insertPlainText(QStringLiteral("\U0001f642\n"));
    QCOMPARE(
        window->session().snapshot().source,
        QStringLiteral("a\U0001f642\r\nb\r\nc\u2029d").toUtf8().toStdString());
    window->undo();
    QCOMPARE(window->session().snapshot().source, source.toStdString());
    QVERIFY(!window->session().dirty());
    QVERIFY(dialogs.errors.empty());
  }

  void namedSaveFailurePreservesState() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("save.md");
    QVERIFY(put(path, "original"));
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.openDocument(path);
    QVERIFY(window);
    window->editor()->insertPlainText("edit ");
    const auto before = window->session().snapshot();
    const auto originalPath = window->session().path();
    QVERIFY(QFile::remove(path));
    QVERIFY(QDir().mkdir(path));
    dialogs.closeChoice = CloseChoice::kSave;
    QVERIFY(!window->close());
    QVERIFY(!dialogs.errors.empty());
    QVERIFY(window->isVisible());
    QCOMPARE(window->session().snapshot().source, before.source);
    QCOMPARE(window->session().snapshot().token, before.token);
    QCOMPARE(window->session().path(), originalPath);
    QVERIFY(window->session().dirty());
    window->undo();
    QCOMPARE(window->session().snapshot().source, std::string("original"));
    QVERIFY(!window->session().dirty());
  }

  void saveAsAndCloseGuards() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText("abc");
    QVERIFY(!window->close());
    QVERIFY(window->isVisible());
    QCOMPARE(dialogs.closePrompts, 1);
    dialogs.closeChoice = CloseChoice::kSave;
    QVERIFY(!window->close());  // Save As cancelled.
    QVERIFY(window->session().dirty());
    dialogs.saveSelection = SaveSelection{dir.filePath("missing/file.md"),
                                          {Encoding::kUtf8, false}};
    QVERIFY(!window->close());
    QVERIFY(!dialogs.errors.empty());
    QVERIFY(window->session().path().empty());
    dialogs.saveSelection =
        SaveSelection{dir.filePath("saved.md"), {Encoding::kUtf16Le, true}};
    QVERIFY(window->saveDocumentAs());
    QCOMPARE(get(dir.filePath("saved.md")),
             QByteArray::fromHex("fffe610062006300"));
    QVERIFY(!window->session().dirty());
    auto cursor = window->editor()->textCursor();
    cursor.movePosition(QTextCursor::End);
    window->editor()->setTextCursor(cursor);
    window->editor()->insertPlainText("d");
    QPointer<MainWindow> observed(window);
    QVERIFY(window->close());  // Save current path, preserving encoding/BOM.
    QCOMPARE(get(dir.filePath("saved.md")),
             QByteArray::fromHex("fffe6100620063006400"));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(observed.isNull());
    auto* discard = controller.newDocument();
    discard->editor()->insertPlainText("discard");
    dialogs.closeChoice = CloseChoice::kDiscard;
    QVERIFY(discard->close());
  }

  void exitCancellationAndFileEvents() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("event.md");
    QVERIFY(put(path, "event\n"));
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    controller.start({});
    auto* initial = controller.windows().front();
    QFileOpenEvent event(path);
    QVERIFY(QCoreApplication::sendEvent(qApp, &event));
    QCOMPARE(controller.windows().size(), 1);
    QCOMPARE(initial->session().snapshot().source, std::string("event\n"));
    initial->editor()->insertPlainText("dirty");
    controller.closeAll();
    QVERIFY(initial->isVisible());
    const auto count = controller.windows().size();
    controller.start({path, path});
    QCOMPARE(controller.windows().size(), count + 2);
    dialogs.closeChoice = CloseChoice::kDiscard;
    controller.closeAll();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(controller.windows().empty());
  }

  void highlightingDoesNotChangeSource() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("highlight.md");
    const QByteArray source = "# Heading\n\n```cpp\nint x;\n```\n\n- item\n";
    QVERIFY(put(path, source));
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.openDocument(path);
    QVERIFY(window);
    QCoreApplication::processEvents();
    const auto doc = window->editor()->document();
    QVERIFY(!doc->firstBlock().layout()->formats().isEmpty());
    QCOMPARE(doc->findBlockByNumber(2).userState(), 6);
    QCOMPARE(doc->findBlockByNumber(3).userState(), 6);
    QCOMPARE(doc->findBlockByNumber(4).userState(), 0);
    QCOMPARE(window->session().snapshot().source, source.toStdString());
    QCOMPARE(window->session().snapshot().token.revision, std::uint64_t{0});
    QVERIFY(!window->session().dirty());
  }

  void markdownHighlightingDistinguishesBoldAndItalic() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("emphasis.md");
    const QByteArray source =
        "**bold** and *italic* and __strong__ and _em_\n";
    QVERIFY(put(path, source));
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.openDocument(path);
    QVERIFY(window);
    QCoreApplication::processEvents();
    const auto formats = window->editor()->document()
                             ->firstBlock()
                             .layout()
                             ->formats();
    const auto formatCovers = [&](int position, auto predicate) {
      return std::any_of(formats.begin(), formats.end(),
                         [&](const QTextLayout::FormatRange& range) {
                           return position >= range.start &&
                                  position < range.start + range.length &&
                                  predicate(range.format);
                         });
    };
    QVERIFY(formatCovers(2, [](const QTextCharFormat& format) {
      return format.fontWeight() >= QFont::Bold;
    }));
    QVERIFY(!formatCovers(2, [](const QTextCharFormat& format) {
      return format.fontItalic();
    }));
    QVERIFY(formatCovers(17, [](const QTextCharFormat& format) {
      return format.fontItalic();
    }));
    QVERIFY(!formatCovers(17, [](const QTextCharFormat& format) {
      return format.fontWeight() >= QFont::Bold;
    }));
    QVERIFY(formatCovers(32, [](const QTextCharFormat& format) {
      return format.fontWeight() >= QFont::Bold;
    }));
    QVERIFY(!formatCovers(32, [](const QTextCharFormat& format) {
      return format.fontItalic();
    }));
  }

  void settingsDialogUsesTabs() {
    SettingsDialog dialog({QStringLiteral("en")});
    auto* tabs = dialog.findChild<QTabWidget*>(
        QStringLiteral("settingsTabs"));
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 4);
    QCOMPARE(tabs->tabText(0), QStringLiteral("General"));
    QCOMPARE(tabs->tabText(1), QStringLiteral("Editor"));
    QCOMPARE(tabs->tabText(2), QStringLiteral("Page"));
    QCOMPARE(tabs->tabText(3), QStringLiteral("Shortcut"));
    QVERIFY(dialog.findChild<QWidget*>(QStringLiteral("settingsGeneralTab")));
    QVERIFY(dialog.findChild<QWidget*>(QStringLiteral("settingsEditorTab")));
    QVERIFY(dialog.findChild<QWidget*>(QStringLiteral("settingsPageTab")));
    QVERIFY(dialog.findChild<QWidget*>(QStringLiteral("settingsShortcutTab")));
    dialog.setShortcutCatalog(
        {{QStringLiteral("boldAction"), QStringLiteral("Bold"),
          QStringLiteral("Ctrl+B")},
         {QStringLiteral("saveAction"), QStringLiteral("Save"),
          QStringLiteral("Ctrl+S")}});
    auto* catalog = dialog.findChild<QTableWidget*>(
        QStringLiteral("settingsShortcutCatalog"));
    QVERIFY(catalog);
    QCOMPARE(catalog->rowCount(), 2);
    QCOMPARE(catalog->item(0, 0)->text(), QStringLiteral("Bold"));
    QCOMPARE(catalog->item(0, 1)->text(), QStringLiteral("Ctrl+B"));
    QCOMPARE(catalog->item(1, 2)->text(), QStringLiteral("saveAction"));
    QVERIFY(dialog.findChild<QWidget*>(
        QStringLiteral("settingsWysiwygBodyFont")));
    QVERIFY(dialog.findChild<QWidget*>(
        QStringLiteral("settingsWysiwygBodySize")));
    QVERIFY(dialog.findChild<QWidget*>(
        QStringLiteral("settingsWysiwygHeading1Size")));
    QVERIFY(dialog.findChild<QWidget*>(
        QStringLiteral("settingsWysiwygHeading6Size")));
    QVERIFY(dialog.findChild<QWidget*>(
        QStringLiteral("settingsWysiwygCodeFont")));
    QVERIFY(dialog.findChild<QWidget*>(
        QStringLiteral("settingsWysiwygQuoteFont")));
  }

  void continuousTextAndHistory() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray source =
        "# Heading\r\n\r\nBefore **bold** after\r\n\r\n"
        "<script>ignored()</script>\r\n\r\n"
        "| A | B |\r\n| - | - |\r\n| x | y |\r\n";
    const auto path = dir.filePath("rich.md");
    QVERIFY(put(path, source));
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.openDocument(path);
    const auto initial = window->session().snapshot();
    action(window, "continuousModeAction")->trigger();
    QVERIFY(window->continuousMode());
    auto* rich = window->continuousEditor()->editor();
    QVERIFY(rich->isVisible());
    QVERIFY(rich->toPlainText().contains("Before bold after"));
    QVERIFY(rich->toPlainText().contains("[Read-only: HTML"));
    QVERIFY(rich->toPlainText().contains(QStringLiteral("x")));
    QCOMPARE(window->session().snapshot().token, initial.token);
    QVERIFY(!window->session().dirty());
    auto cursor = rich->document()->find("bold");
    QVERIFY(!cursor.isNull());
    QVERIFY(cursor.charFormat().fontWeight() >= QFont::Bold);
    rich->setTextCursor(cursor);
    rich->insertPlainText("better");
    auto expected = source;
    expected.replace("**bold**", "**better**");
    QCOMPARE(window->session().snapshot().source, expected.toStdString());
    QVERIFY(window->session().dirty());
    QVERIFY(window->saveDocument());
    QCOMPARE(get(path), expected);
    QVERIFY(!window->session().dirty());
    window->activateWindow();
    rich->setFocus();
    QCoreApplication::processEvents();
    QTest::keySequence(rich, QKeySequence(QKeySequence::Undo));
    QCOMPARE(window->session().snapshot().source, source.toStdString());
    QVERIFY(window->session().dirty());
    action(window, "markdownModeAction")->trigger();
    QVERIFY(!window->continuousMode());
    QVERIFY(window->editor()->toPlainText().contains("**bold**"));
    window->redo();
    QCOMPARE(window->session().snapshot().source, expected.toStdString());
    QVERIFY(!window->session().dirty());
    window->setContinuousMode(true);
    rich->moveCursor(QTextCursor::Start);
    QTest::keyClicks(rich, "XYZ");
    auto typed = expected;
    typed.replace("# Heading", "# XYZHeading");
    QCOMPARE(window->session().snapshot().source, typed.toStdString());
    window->undo();
    window->undo();
    window->undo();
    QCOMPARE(window->session().snapshot().source, expected.toStdString());
    window->setContinuousMode(false);
    window->editor()->moveCursor(QTextCursor::End);
    window->editor()->insertPlainText("branch");
    const auto branched = window->session().snapshot().source;
    window->setContinuousMode(true);
    window->redo();
    QCOMPARE(window->session().snapshot().source, branched);
    QVERIFY(dialogs.errors.empty());
  }

  void continuousProtectedEdits_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<QString>("selection");
    QTest::addColumn<QString>("replacement");
    QTest::newRow("autolink")
        << QByteArray("<https://invalid.test>\n")
        << QStringLiteral("https://invalid.test") << QStringLiteral("x");
    QTest::newRow("html") << QByteArray("<script>bad()</script>\n")
                          << QStringLiteral("Read-only") << QStringLiteral("x");
    QTest::newRow("delete-run")
        << QByteArray("word\n") << QStringLiteral("word") << QString{};
    QTest::newRow("newline") << QByteArray("word\n") << QStringLiteral("word")
                             << QStringLiteral("two\nlines");
    QTest::newRow("nul") << QByteArray("word\n") << QStringLiteral("word")
                         << QString(QChar(0));
    QTest::newRow("surrogate") << QByteArray("word\n") << QStringLiteral("word")
                               << QString(QChar(0xd800));
    QTest::newRow("cross-spans")
        << QByteArray("one **two** three\n") << QStringLiteral("one two")
        << QStringLiteral("x");
    QTest::newRow("cross-blocks")
        << QByteArray("one\n\ntwo\n") << QStringLiteral("one")
        << QStringLiteral("x");
  }

  void continuousProtectedEdits() {
    QFETCH(QByteArray, source);
    QFETCH(QString, selection);
    QFETCH(QString, replacement);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("protected.md");
    QVERIFY(put(path, source));
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.openDocument(path);
    QVERIFY(window->setContinuousMode(true));
    auto* rich = window->continuousEditor()->editor();
    const auto displayed = rich->toPlainText();
    const auto before = window->session().snapshot();
    auto cursor = rich->document()->find(selection);
    QVERIFY(!cursor.isNull());
    rich->setTextCursor(cursor);
    if (QByteArray(QTest::currentDataTag()) == "cross-blocks") {
      rich->selectAll();
    }
    rich->insertPlainText(replacement);
    QCOMPARE(window->session().snapshot().source, before.source);
    QCOMPARE(window->session().snapshot().token, before.token);
    QVERIFY(!window->session().dirty());
    QCOMPARE(rich->toPlainText(), displayed);
    QVERIFY(!dialogs.errors.empty());
    QVERIFY(!action(window, "undoAction")->isEnabled());
    QVERIFY(window->saveDocument());
    QCOMPARE(get(path), source);
  }

  void continuousCommandsAndStaleSnapshots() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray source = "title\r\n\r\n- [ ] todo\r\n";
    const auto path = dir.filePath("commands.md");
    QVERIFY(put(path, source));
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.openDocument(path);
    QVERIFY(window->setContinuousMode(true));
    auto* rich = window->continuousEditor()->editor();
    rich->moveCursor(QTextCursor::Start);
    QVERIFY(action(window, "heading2Action")->isEnabled());
    const auto oldToken = window->session().snapshot().token;
    action(window, "heading2Action")->trigger();
    QCOMPARE(window->session().snapshot().source,
             std::string("## title\r\n\r\n- [ ] todo\r\n"));
    auto cursor = rich->document()->find("todo");
    QVERIFY(!cursor.isNull());
    rich->setTextCursor(cursor);
    QVERIFY(action(window, "toggleTaskAction")->isEnabled());
    QVERIFY(!action(window, "heading1Action")->isEnabled());
    action(window, "toggleTaskAction")->trigger();
    QCOMPARE(window->session().snapshot().source,
             std::string("## title\r\n\r\n- [x] todo\r\n"));
    const auto current = window->session().snapshot();
    QVERIFY(!window->applyContinuousCommand(oldToken,
                                            markdown::SetHeadingLevel{1, 1}));
    QCOMPARE(window->session().snapshot().source, current.source);
    QCOMPARE(window->session().snapshot().token, current.token);
    QVERIFY(!dialogs.errors.empty());
    auto* other = controller.newDocument();
    other->editor()->insertPlainText("other");
    QVERIFY(!window->applyContinuousCommand(other->session().snapshot().token,
                                            markdown::SetHeadingLevel{1, 1}));
    QCOMPARE(window->session().snapshot().source, current.source);
    window->undo();
    QCOMPARE(window->session().snapshot().source,
             std::string("## title\r\n\r\n- [ ] todo\r\n"));
    window->undo();
    QCOMPARE(window->session().snapshot().source, source.toStdString());
    QVERIFY(!window->session().dirty());
    QCOMPARE(other->session().snapshot().source, std::string("other"));
  }

  void continuousSelectionAndPlainPaste() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("unicode-rich.md");
    const auto source =
        QStringLiteral("# Head\r\n\r\ntext **\U0001f600\u00a0word** tail\n")
            .toUtf8();
    QVERIFY(put(path, source));
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.openDocument(path);
    auto cursor = window->editor()->textCursor();
    const int unicodeStart = window->editor()->document()->toRawText().indexOf(
        QStringLiteral("\U0001f600\u00a0word"));
    QVERIFY(unicodeStart >= 0);
    cursor.setPosition(unicodeStart);
    cursor.setPosition(unicodeStart + 7, QTextCursor::KeepAnchor);
    window->editor()->setTextCursor(cursor);
    const int originalAnchor = cursor.anchor();
    const int originalPosition = cursor.position();
    window->setContinuousMode(true);
    auto* rich = window->continuousEditor()->editor();
    QCOMPARE(rich->textCursor().selectedText(),
             QStringLiteral("\U0001f600\u00a0word"));
    window->setContinuousMode(false);
    QCOMPARE(window->editor()->textCursor().anchor(), originalAnchor);
    QCOMPARE(window->editor()->textCursor().position(), originalPosition);
    window->setContinuousMode(true);
    auto* data = new QMimeData;
    data->setText(QStringLiteral("safe"));
    data->setHtml(
        QStringLiteral("<img src=\"https://invalid.test/x\"><b>unsafe</b>"));
    QApplication::clipboard()->setMimeData(data);
    rich->paste();
    auto expected = source;
    expected.replace(QStringLiteral("\U0001f600\u00a0word").toUtf8(), "safe");
    QCOMPARE(window->session().snapshot().source, expected.toStdString());
    QVERIFY(!rich->toPlainText().contains("unsafe"));
    window->undo();
    QCOMPARE(window->session().snapshot().source, source.toStdString());
    QVERIFY(!window->session().dirty());
    QVERIFY(dialogs.errors.empty());
    auto sourceCursor = window->editor()->textCursor();
    sourceCursor.setPosition(0);  // Heading marker has no rendered counterpart.
    window->setContinuousMode(false);
    window->editor()->setTextCursor(sourceCursor);
    window->setContinuousMode(true);
    QVERIFY(window->continuousEditor()
                ->findChild<QLabel*>("continuousNotice")
                ->text()
                .contains("nearest mapped boundary"));
    QCOMPARE(window->session().snapshot().source, source.toStdString());
    QApplication::clipboard()->clear();
  }

  void continuousMixedSourcePreservation_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::newRow("mixed-eol") << QByteArray("word\r\n\r\nother\rthird\n");
    QTest::newRow("literal-bom")
        << QByteArray::fromHex("efbbbfefbbbf") + "word\n";
    QTest::newRow("definitions")
        << QByteArray("word\n\n[id]: /private \"title\"\n");
    QTest::newRow("nested") << QByteArray("> - **word**\n");
    QTest::newRow("entity") << QByteArray("word &amp; tail\n");
  }

  void continuousMixedSourcePreservation() {
    QFETCH(QByteArray, source);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("preserved.md");
    QVERIFY(put(path, source));
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.openDocument(path);
    const auto before = window->session().snapshot();
    QVERIFY(window->setContinuousMode(true));
    auto* rich = window->continuousEditor()->editor();
    auto cursor = rich->document()->find("word");
    QVERIFY(!cursor.isNull());
    rich->setTextCursor(cursor);
    rich->insertPlainText("changed");
    auto expected = before.source;
    expected.replace(expected.find("word"), 4, "changed");
    QCOMPARE(window->session().snapshot().source, expected);
    window->undo();
    QCOMPARE(window->session().snapshot().source, before.source);
    QVERIFY(!window->session().dirty());
    QVERIFY(window->saveDocument());
    QCOMPARE(get(path), source);
    QVERIFY(dialogs.errors.empty());
  }

  void continuousAmbiguousRunBoundary() {
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText("one **two** tail\n");
    QVERIFY(window->setContinuousMode(true));
    auto* rich = window->continuousEditor()->editor();
    auto cursor = rich->document()->find("two");
    QVERIFY(!cursor.isNull());
    cursor.setPosition(cursor.selectionStart());
    rich->setTextCursor(cursor);
    const auto before = window->session().snapshot();
    rich->insertPlainText("X");
    QCOMPARE(window->session().snapshot().source, before.source);
    QCOMPARE(window->session().snapshot().token, before.token);
    QVERIFY(!dialogs.errors.empty());
    QVERIFY(dialogs.errors.back().contains("between text runs"));
  }

  void continuousInlineFormatting_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<QString>("selection");
    QTest::addColumn<QByteArray>("actionName");
    QTest::addColumn<QByteArray>("expected");
    QTest::addColumn<bool>("initiallyChecked");
    QTest::newRow("bold") << QByteArray("a word z\r\n")
                          << QStringLiteral("word") << QByteArray("boldAction")
                          << QByteArray("a **word** z\r\n") << false;
    QTest::newRow("italic")
        << QByteArray("a word z\n") << QStringLiteral("word")
        << QByteArray("italicAction") << QByteArray("a *word* z\n") << false;
    QTest::newRow("strike")
        << QByteArray("a word z\n") << QStringLiteral("word")
        << QByteArray("strikethroughAction") << QByteArray("a ~~word~~ z\n")
        << false;
    QTest::newRow("partial")
        << QByteArray("word\n") << QStringLiteral("or")
        << QByteArray("boldAction") << QByteArray("w**or**d\n") << false;
    QTest::newRow("unbold")
        << QByteArray("a __word__ z\n") << QStringLiteral("word")
        << QByteArray("boldAction") << QByteArray("a word z\n") << true;
    QTest::newRow("unitalic")
        << QByteArray("_word_\n") << QStringLiteral("word")
        << QByteArray("italicAction") << QByteArray("word\n") << true;
    QTest::newRow("unstrike")
        << QByteArray("~~word~~\n") << QStringLiteral("word")
        << QByteArray("strikethroughAction") << QByteArray("word\n") << true;
    QTest::newRow("nested")
        << QByteArray("_word_\n") << QStringLiteral("word")
        << QByteArray("boldAction") << QByteArray("_**word**_\n") << false;
    QTest::newRow("heading")
        << QByteArray("# word\n") << QStringLiteral("word")
        << QByteArray("boldAction") << QByteArray("# **word**\n") << false;
    QTest::newRow("unicode")
        << QStringLiteral("a \u0441\u043b\u043e\u0432\u043e\U0001f600 z\n")
               .toUtf8()
        << QStringLiteral("\u0441\u043b\u043e\u0432\u043e\U0001f600")
        << QByteArray("boldAction")
        << QStringLiteral("a **\u0441\u043b\u043e\u0432\u043e\U0001f600** z\n")
               .toUtf8()
        << false;
  }

  void continuousInlineFormatting() {
    QFETCH(QByteArray, source);
    QFETCH(QString, selection);
    QFETCH(QByteArray, actionName);
    QFETCH(QByteArray, expected);
    QFETCH(bool, initiallyChecked);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray prefix = "<div>opaque</div>\r\n\r\n";
    const QByteArray suffix = "\n\n[id]: /keep\n";
    const auto path = dir.filePath("inline.md");
    QVERIFY(put(path, prefix + source + suffix));
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.openDocument(path);
    auto* other = controller.newDocument();
    other->editor()->insertPlainText("independent");
    QVERIFY(window->setContinuousMode(true));
    auto* rich = window->continuousEditor()->editor();
    auto cursor = rich->document()->find(selection);
    QVERIFY(!cursor.isNull());
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    cursor.setPosition(end);
    cursor.setPosition(start, QTextCursor::KeepAnchor);
    rich->setTextCursor(cursor);
    auto* command = action(window, actionName.constData());
    QVERIFY(command);
    QVERIFY(command->isEnabled());
    QCOMPARE(command->isChecked(), initiallyChecked);
    const auto before = window->session().snapshot();
    command->trigger();
    QCOMPARE(window->session().snapshot().source,
             (prefix + expected + suffix).toStdString());
    QCOMPARE(window->session().snapshot().token.revision,
             before.token.revision + 1);
    QCOMPARE(rich->textCursor().anchor(), end);
    QCOMPARE(rich->textCursor().position(), start);
    QCOMPARE(rich->textCursor().selectedText(), selection);
    QVERIFY(command->isEnabled());
    QCOMPARE(command->isChecked(), !initiallyChecked);
    QVERIFY(window->session().dirty());
    QVERIFY(window->saveDocument());
    QCOMPARE(get(path), prefix + expected + suffix);
    window->setContinuousMode(false);
    QVERIFY(command->isEnabled());
    QVERIFY(window->editor()->toPlainText().contains(
        QString::fromUtf8(expected).trimmed()));
    window->undo();
    QCOMPARE(window->session().snapshot().source,
             (prefix + source + suffix).toStdString());
    window->redo();
    QCOMPARE(window->session().snapshot().source,
             (prefix + expected + suffix).toStdString());
    QVERIFY(!window->session().dirty());
    QCOMPARE(other->session().snapshot().source, std::string("independent"));
    QVERIFY(dialogs.errors.empty());
  }

  void continuousInlineKeyboardAndRepeat() {
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText("a word z\n");
    window->setContinuousMode(true);
    window->activateWindow();
    auto* rich = window->continuousEditor()->editor();
    rich->setFocus();
    QCoreApplication::processEvents();
    rich->setTextCursor(rich->document()->find("word"));
    QTest::keySequence(rich, QKeySequence(QKeySequence::Bold));
    QCOMPARE(window->session().snapshot().source,
             std::string("a **word** z\n"));
    QCOMPARE(rich->textCursor().selectedText(), QStringLiteral("word"));
    QTest::keySequence(rich, QKeySequence(QKeySequence::Bold));
    QCOMPARE(window->session().snapshot().source, std::string("a word z\n"));
    QTest::keySequence(rich, QKeySequence(QKeySequence::Italic));
    QCOMPARE(window->session().snapshot().source, std::string("a *word* z\n"));
    QTest::keySequence(rich, QKeySequence(QKeySequence::Italic));
    QCOMPARE(window->session().snapshot().source, std::string("a word z\n"));
    action(window, "strikethroughAction")->trigger();
    QCOMPARE(window->session().snapshot().source,
             std::string("a ~~word~~ z\n"));
    action(window, "strikethroughAction")->trigger();
    QCOMPARE(window->session().snapshot().source, std::string("a word z\n"));
    QVERIFY(dialogs.errors.empty());
    QTest::keySequence(rich, QKeySequence(QKeySequence::Underline));
    QVERIFY(!dialogs.errors.empty());
    QCOMPARE(window->session().snapshot().source, std::string("a word z\n"));
    const auto valid = window->session().snapshot();
    const auto state =
        window->continuousEditor()->inlineState(markdown::InlineStyle::kBold);
    QVERIFY(state.command);
    auto stale = valid.token;
    ++stale.revision;
    QVERIFY(!window->applyContinuousCommand(stale, *state.command));
    QCOMPARE(window->session().snapshot().source, valid.source);
    QCOMPARE(window->session().snapshot().token, valid.token);
  }

  void continuousInlineUnsupportedSelection() {
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText(
        "a **word** z\n\n[link](/keep)\n\n![alt](/image)\n");
    window->setContinuousMode(true);
    auto* rich = window->continuousEditor()->editor();
    auto* bold = action(window, "boldAction");
    QVERIFY(!bold->isEnabled());
    auto cursor = rich->document()->find("or");
    QVERIFY(!cursor.isNull());
    rich->setTextCursor(cursor);
    QVERIFY(!bold->isEnabled());
    QVERIFY(bold->isChecked());
    QVERIFY(bold->toolTip().contains("entire simple span"));
    auto before = window->session().snapshot();
    window->continuousEditor()->toggleInlineStyle(markdown::InlineStyle::kBold);
    QCOMPARE(window->session().snapshot().source, before.source);
    QCOMPARE(window->session().snapshot().token, before.token);
    QVERIFY(!dialogs.errors.empty());
    rich->selectAll();
    QVERIFY(!bold->isEnabled());
    window->continuousEditor()->toggleInlineStyle(markdown::InlineStyle::kBold);
    QCOMPARE(window->session().snapshot().token, before.token);
    cursor = rich->document()->find("link");
    rich->setTextCursor(cursor);
    QVERIFY(!bold->isEnabled());
    cursor = rich->document()->find("alt");
    QVERIFY(!cursor.isNull());
    rich->setTextCursor(cursor);
    QVERIFY(!bold->isEnabled());
    window->continuousEditor()->toggleInlineStyle(markdown::InlineStyle::kBold);
    QCOMPARE(window->session().snapshot().source, before.source);
    QCOMPARE(window->session().snapshot().token, before.token);
    cursor = rich->document()->find(" z");
    rich->setTextCursor(cursor);
    QVERIFY(!bold->isEnabled());
    window->continuousEditor()->toggleInlineStyle(markdown::InlineStyle::kBold);
    QCOMPARE(window->session().snapshot().source, before.source);
    QCOMPARE(window->session().snapshot().token, before.token);
    window->undo();
    QCOMPARE(window->session().snapshot().source, std::string{});
    window->redo();
    QCOMPARE(window->session().snapshot().source, before.source);
  }

  void continuousLinkLabels_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<QString>("label");
    QTest::addColumn<QString>("replacement");
    QTest::addColumn<QByteArray>("expected");
    QTest::newRow("inline")
        << QByteArray("[before](/keep \"title\")\r\n")
        << QStringLiteral("before") << QStringLiteral("after")
        << QByteArray("[after](/keep \"title\")\r\n");
    QTest::newRow("reference")
        << QByteArray("[before][id] and [other][id]\n\n[id]: /keep \"title\"\n")
        << QStringLiteral("before") << QStringLiteral("after")
        << QByteArray("[after][id] and [other][id]\n\n[id]: /keep \"title\"\n");
    QTest::newRow("bold") << QByteArray("[**before**](/keep)\n")
                          << QStringLiteral("before") << QStringLiteral("after")
                          << QByteArray("[**after**](/keep)\n");
    QTest::newRow("quote-list")
        << QByteArray("> - [before](/keep)\n") << QStringLiteral("before")
        << QStringLiteral("after") << QByteArray("> - [after](/keep)\n");
    QTest::newRow("unicode")
        << QStringLiteral("[\u0441\u043b\u043e\u0432\u043e\U0001f600](/keep)\n")
               .toUtf8()
        << QStringLiteral("\u0441\u043b\u043e\u0432\u043e\U0001f600")
        << QStringLiteral("\U0001f642\u0442\u0435\u043a\u0441\u0442")
        << QStringLiteral("[\U0001f642\u0442\u0435\u043a\u0441\u0442](/keep)\n")
               .toUtf8();
    QTest::newRow("javascript-inert")
        << QByteArray("[before](javascript:ignored)\n")
        << QStringLiteral("before") << QStringLiteral("after")
        << QByteArray("[after](javascript:ignored)\n");
    QTest::newRow("file-inert")
        << QByteArray("[before](file:///private)\n") << QStringLiteral("before")
        << QStringLiteral("after") << QByteArray("[after](file:///private)\n");
    QTest::newRow("data-inert")
        << QByteArray("[before](data:text/plain,ignored)\n")
        << QStringLiteral("before") << QStringLiteral("after")
        << QByteArray("[after](data:text/plain,ignored)\n");
    QTest::newRow("punctuation")
        << QByteArray("[before](/keep)\n") << QStringLiteral("before")
        << QStringLiteral("a]b") << QByteArray("[a\\]b](/keep)\n");
  }

  void continuousLinkLabels() {
    QFETCH(QByteArray, source);
    QFETCH(QString, label);
    QFETCH(QString, replacement);
    QFETCH(QByteArray, expected);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("links.md");
    const QByteArray prefix = "<div>opaque</div>\n\n";
    QVERIFY(put(path, prefix + source));
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.openDocument(path);
    const auto before = window->session().snapshot();
    QVERIFY(window->setContinuousMode(true));
    auto* rich = window->continuousEditor()->editor();
    QVERIFY(!rich->toPlainText().contains("[Read-only: link]"));
    auto cursor = rich->document()->find(label);
    QVERIFY(!cursor.isNull());
    QVERIFY(!cursor.charFormat().isAnchor());
    QVERIFY(cursor.charFormat().anchorHref().isEmpty());
    QVERIFY(cursor.charFormat().toolTip().contains("navigation disabled"));
    cursor.setPosition(cursor.selectionEnd());
    cursor.setPosition(cursor.position() - label.size(),
                       QTextCursor::KeepAnchor);
    rich->setTextCursor(cursor);
    QVERIFY(!action(window, "boldAction")->isEnabled());
    QVERIFY(!action(window, "italicAction")->isEnabled());
    QVERIFY(!action(window, "strikethroughAction")->isEnabled());
    window->setContinuousMode(false);
    QCOMPARE(window->editor()->textCursor().selectedText(), label);
    window->setContinuousMode(true);
    QCOMPARE(rich->textCursor().selectedText(), label);
    rich->insertPlainText(replacement);
    QCOMPARE(window->session().snapshot().source,
             (prefix + expected).toStdString());
    QVERIFY(window->session().dirty());
    QVERIFY(rich->toPlainText().contains(replacement));
    QVERIFY(window->saveDocument());
    QCOMPARE(get(path), prefix + expected);
    window->undo();
    QCOMPARE(window->session().snapshot().source, before.source);
    QVERIFY(window->session().dirty());
    window->setContinuousMode(false);
    window->redo();
    QCOMPARE(window->session().snapshot().source,
             (prefix + expected).toStdString());
    QVERIFY(!window->session().dirty());
    QVERIFY(dialogs.errors.empty());
  }

  void continuousLinkRejectedEdits_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<QString>("selection");
    QTest::addColumn<QString>("replacement");
    QTest::newRow("autolink-http")
        << QByteArray("<https://invalid.test>\n")
        << QStringLiteral("https://invalid.test") << QStringLiteral("changed");
    QTest::newRow("autolink-gfm")
        << QByteArray("https://invalid.test\n")
        << QStringLiteral("https://invalid.test") << QStringLiteral("changed");
    QTest::newRow("autolink-www")
        << QByteArray("www.invalid.test\n")
        << QStringLiteral("www.invalid.test") << QStringLiteral("changed");
    QTest::newRow("autolink-email")
        << QByteArray("<user@invalid.test>\n")
        << QStringLiteral("user@invalid.test") << QStringLiteral("changed");
    QTest::newRow("collapsed")
        << QByteArray("[before][]\n\n[before]: /keep\n")
        << QStringLiteral("before") << QStringLiteral("after");
    QTest::newRow("shortcut")
        << QByteArray("[before]\n\n[before]: /keep\n")
        << QStringLiteral("before") << QStringLiteral("after");
    QTest::newRow("empty") << QByteArray("[before](/keep)\n")
                           << QStringLiteral("before") << QString{};
    QTest::newRow("newline")
        << QByteArray("[before](/keep)\n") << QStringLiteral("before")
        << QStringLiteral("two\nlines");
    QTest::newRow("cross-runs")
        << QByteArray("[one **two**](/keep)\n") << QStringLiteral("one two")
        << QStringLiteral("after");
    QTest::newRow("code-child")
        << QByteArray("[`code`](/keep)\n") << QStringLiteral("Read-only: code")
        << QStringLiteral("after");
    QTest::newRow("entity-child")
        << QByteArray("[a &amp; b](/keep)\n") << QStringLiteral("&amp;")
        << QStringLiteral("after");
  }

  void continuousLinkRejectedEdits() {
    QFETCH(QByteArray, source);
    QFETCH(QString, selection);
    QFETCH(QString, replacement);
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText(QString::fromUtf8(source));
    const auto before = window->session().snapshot();
    window->setContinuousMode(true);
    auto* rich = window->continuousEditor()->editor();
    const auto displayed = rich->document()->toRawText();
    auto cursor = rich->document()->find(selection);
    QVERIFY(!cursor.isNull());
    rich->setTextCursor(cursor);
    rich->insertPlainText(replacement);
    QCOMPARE(window->session().snapshot().source, before.source);
    QCOMPARE(window->session().snapshot().token, before.token);
    QCOMPARE(rich->document()->toRawText(), displayed);
    QVERIFY(!dialogs.errors.empty());
    window->undo();
    QCOMPARE(window->session().snapshot().source, std::string{});
    window->redo();
    QCOMPARE(window->session().snapshot().source, before.source);
  }

  void continuousLinkNavigationDisabled() {
    RecordedUrlHandlers recorder;
    QVERIFY(QDesktopServices::openUrl(
        QUrl("https://invalid.test/recording-check")));
    QCOMPARE(recorder.urls.size(), 1);
    recorder.urls.clear();
    const QByteArray source =
        "[web](https://invalid.test) [mail](mailto:user@invalid.test) "
        "[local](file:///private) [script](javascript:ignored) "
        "[payload](data:text/plain,ignored)\n";
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText(QString::fromUtf8(source));
    window->setContinuousMode(true);
    window->activateWindow();
    auto* rich = window->continuousEditor()->editor();
    rich->setFocus();
    QCoreApplication::processEvents();
    const auto before = window->session().snapshot();
    for (const QString& label :
         {QStringLiteral("web"), QStringLiteral("mail"),
          QStringLiteral("local"), QStringLiteral("script"),
          QStringLiteral("payload")}) {
      auto cursor = rich->document()->find(label);
      QVERIFY(!cursor.isNull());
      QVERIFY(!cursor.charFormat().isAnchor());
      QVERIFY(cursor.charFormat().anchorHref().isEmpty());
      QVERIFY(!cursor.charFormat().isImageFormat());
      cursor.setPosition(cursor.selectionStart() + 1);
      rich->setTextCursor(cursor);
      const auto point = rich->cursorRect().center();
      QVERIFY(rich->anchorAt(point).isEmpty());
      QTest::mouseClick(rich->viewport(), Qt::LeftButton, Qt::NoModifier,
                        point);
      QTest::mouseClick(rich->viewport(), Qt::LeftButton, Qt::ControlModifier,
                        point);
    }
    QCOMPARE(recorder.urls.size(), 0);
    QTest::keyClick(rich, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(recorder.urls.size(), 0);
    QVERIFY(dialogs.errors.empty());
    QCOMPARE(window->session().snapshot().source, before.source);
    QCOMPARE(window->session().snapshot().token, before.token);
  }

  void continuousLinkKeyboard() {
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText("[before](/keep)\n");
    window->setContinuousMode(true);
    window->activateWindow();
    auto* rich = window->continuousEditor()->editor();
    rich->setFocus();
    QCoreApplication::processEvents();
    auto cursor = rich->document()->find("before");
    cursor.setPosition(cursor.selectionStart() + 2);
    rich->setTextCursor(cursor);
    QTest::keyClicks(rich, "XYZ");
    QCOMPARE(window->session().snapshot().source,
             std::string("[beXYZfore](/keep)\n"));
    QCOMPARE(rich->textCursor().position(), 5);
    QTest::keySequence(rich, QKeySequence(QKeySequence::Undo));
    QCOMPARE(window->session().snapshot().source,
             std::string("[beXYfore](/keep)\n"));
    QTest::keySequence(rich, QKeySequence(QKeySequence::Redo));
    QCOMPARE(window->session().snapshot().source,
             std::string("[beXYZfore](/keep)\n"));
    QVERIFY(dialogs.errors.empty());
  }

  void continuousLinkClipboardAndStaleToken() {
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText("[before](/keep \"title\")\n");
    window->setContinuousMode(true);
    auto* rich = window->continuousEditor()->editor();
    rich->setTextCursor(rich->document()->find("before"));
    const auto token = window->session().snapshot().token;
    const auto start = window->continuousEditor()->sourcePosition(
        rich->textCursor().selectionStart());
    QVERIFY(start);
    auto parsed = markdown::parse("[before](/keep \"title\")\n");
    QVERIFY(parsed.document);
    std::optional<size_t> node;
    for (size_t index = 0; index < parsed.document->nodes.size(); ++index) {
      if (parsed.document->nodes[index].text == "before") {
        node = index;
      }
    }
    QVERIFY(node);
    auto* data = new QMimeData;
    data->setText("after");
    data->setHtml(
        "<img src=\"https://invalid.test/resource\"><script>ignored</script>");
    QApplication::clipboard()->setMimeData(data);
    rich->paste();
    QCOMPARE(window->session().snapshot().source,
             std::string("[after](/keep \"title\")\n"));
    QVERIFY(!rich->toPlainText().contains("script"));
    QVERIFY(!window->applyContinuousCommand(
        token, markdown::ReplaceText{*node, "stale"}));
    QCOMPARE(window->session().snapshot().source,
             std::string("[after](/keep \"title\")\n"));
    window->undo();
    QCOMPARE(window->session().snapshot().source,
             std::string("[before](/keep \"title\")\n"));
    QApplication::clipboard()->clear();
  }

  void pagedModeLayoutAndSettings() {
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    QByteArray source;
    for (int i = 0; i < 120; ++i) {
      source += "Paragraph " + QByteArray::number(i) + "\n\n";
    }
    window->editor()->setPlainText(QString::fromUtf8(source));
    QVERIFY(window->setPagedMode(true));
    QVERIFY(window->pagedMode());
    QVERIFY(window->continuousMode());
    const int firstCount = window->continuousEditor()->pageCount();
    QVERIFY(firstCount >= 2);

    Settings settings;
    settings.page.marginTopMm = 80.0;
    settings.page.marginBottomMm = 80.0;
    settings.page.marginLeftMm = 50.0;
    settings.page.marginRightMm = 50.0;
    QString error;
    QVERIFY2(window->applySettings(settings, &error), qPrintable(error));
    const int secondCount = window->continuousEditor()->pageCount();
    QVERIFY(secondCount >= firstCount);

    window->continuousEditor()->editor()->moveCursor(QTextCursor::End);
    QVERIFY(window->continuousEditor()->currentPage() >= 0);
    QVERIFY(window->continuousEditor()->currentPage() <
            window->continuousEditor()->pageCount());

    QVERIFY(window->setContinuousMode(false));
    QVERIFY(!window->continuousMode());
    QVERIFY(!window->pagedMode());
  }

  void sketchMenuStructure() {
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    const auto names = [](QMenu* menu) {
      QStringList result;
      for (auto* action : menu->actions()) {
        if (!action->isSeparator()) {
          result.append(action->objectName());
        }
      }
      return result;
    };
    auto* file = window->findChild<QMenu*>(QStringLiteral("fileMenu"));
    QVERIFY(file);
    QCOMPARE(names(file),
             QStringList({QStringLiteral("newAction"),
                          QStringLiteral("openAction"),
                          QStringLiteral("saveAction"),
                          QStringLiteral("saveAsAction"),
                          QStringLiteral("exportAction"),
                          QStringLiteral("printAction"),
                          QStringLiteral("exitAction")}));
    auto* exportMenu =
        window->findChild<QMenu*>(QStringLiteral("exportMenu"));
    QVERIFY(exportMenu);
    QCOMPARE(names(exportMenu),
             QStringList({QStringLiteral("exportPdfAction"),
                          QStringLiteral("exportHtmlAction")}));
    auto* edit = window->findChild<QMenu*>(QStringLiteral("editMenu"));
    QVERIFY(edit);
    QCOMPARE(names(edit),
             QStringList({QStringLiteral("undoAction"),
                          QStringLiteral("redoAction"),
                          QStringLiteral("cutAction"),
                          QStringLiteral("copyAction"),
                          QStringLiteral("pasteAction"),
                          QStringLiteral("settingsAction")}));
    auto* format = window->findChild<QMenu*>(QStringLiteral("formatMenu"));
    QVERIFY(format);
    QCOMPARE(names(format),
             QStringList({QStringLiteral("headingMenuAction"),
                          QStringLiteral("boldAction"),
                          QStringLiteral("italicAction"),
                          QStringLiteral("strikethroughAction"),
                          QStringLiteral("blockquoteAction"),
                          QStringLiteral("codeAction"),
                          QStringLiteral("horizontalLineAction"),
                          QStringLiteral("linkAction"),
                          QStringLiteral("imageAction"),
                          QStringLiteral("tableAction"),
                          QStringLiteral("listMenuAction")}));
    auto* headingMenu =
        window->findChild<QMenu*>(QStringLiteral("headingMenu"));
    QVERIFY(headingMenu);
    QCOMPARE(names(headingMenu),
             QStringList({QStringLiteral("heading1Action"),
                          QStringLiteral("heading2Action"),
                          QStringLiteral("heading3Action"),
                          QStringLiteral("heading4Action"),
                          QStringLiteral("heading5Action"),
                          QStringLiteral("heading6Action")}));
    auto* listMenu = window->findChild<QMenu*>(QStringLiteral("listMenu"));
    QVERIFY(listMenu);
    QCOMPARE(names(listMenu),
             QStringList({QStringLiteral("orderedListAction"),
                          QStringLiteral("unorderedListAction")}));
    auto* view = window->findChild<QMenu*>(QStringLiteral("viewMenu"));
    QVERIFY(view);
    QCOMPARE(names(view),
             QStringList({QStringLiteral("fullScreenAction"),
                          QStringLiteral("zoomInAction"),
                          QStringLiteral("zoomOutAction"),
                          QStringLiteral("resetZoomAction"),
                          QStringLiteral("switchModeAction")}));
    auto* modes =
        window->findChild<QMenu*>(QStringLiteral("switchModeMenu"));
    QVERIFY(modes);
    QCOMPARE(names(modes),
             QStringList({QStringLiteral("markdownModeAction"),
                          QStringLiteral("continuousModeAction"),
                          QStringLiteral("pagedModeAction")}));
    auto* help = window->findChild<QMenu*>(QStringLiteral("helpMenu"));
    QVERIFY(help);
    QVERIFY(help->actions().isEmpty());

    auto* toolbar =
        window->findChild<QToolBar*>(QStringLiteral("mainToolBar"));
    QVERIFY(toolbar);
    const QStringList toolbarNames{
        QStringLiteral("newAction"),
        QStringLiteral("openAction"),
        QStringLiteral("saveAction"),
        QStringLiteral("undoAction"),
        QStringLiteral("redoAction"),
        QStringLiteral("settingsAction"),
        QStringLiteral("headingMenuAction"),
        QStringLiteral("boldAction"),
        QStringLiteral("italicAction"),
        QStringLiteral("strikethroughAction"),
        QStringLiteral("blockquoteAction"),
        QStringLiteral("codeAction"),
        QStringLiteral("horizontalLineAction"),
        QStringLiteral("linkAction"),
        QStringLiteral("imageAction"),
        QStringLiteral("tableAction"),
        QStringLiteral("listMenuAction"),
        QStringLiteral("orderedListAction"),
        QStringLiteral("unorderedListAction"),
        QStringLiteral("switchModeAction")};
    QStringList actualToolbar;
    for (auto* action : toolbar->actions()) {
      if (!action->isSeparator()) {
        actualToolbar.append(action->objectName());
      }
    }
    QCOMPARE(actualToolbar, toolbarNames);
    QVERIFY(window->statusBar()->currentMessage().contains(
        QStringLiteral("Chars")));
    QVERIFY(window->statusBar()->currentMessage().contains(
        QStringLiteral("Words")));
    QVERIFY(window->statusBar()->currentMessage().contains(
        QStringLiteral("Lines")));
  }

  void toolbarIcons() {
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    auto* toolbar =
        window->findChild<QToolBar*>(QStringLiteral("mainToolBar"));
    QVERIFY(toolbar);
    QCOMPARE(toolbar->toolButtonStyle(), Qt::ToolButtonIconOnly);
    QCOMPARE(toolbar->iconSize(), QSize(20, 20));
    int count = 0;
    for (auto* value : toolbar->actions()) {
      if (!value || value->isSeparator()) {
        continue;
      }
      ++count;
      QVERIFY(!value->objectName().isEmpty());
      QVERIFY(!value->text().isEmpty());
      QVERIFY(!value->toolTip().isEmpty());
      QVERIFY(!value->icon().isNull());
      QVERIFY(!value->icon().pixmap(20, QIcon::Normal).isNull());
      QVERIFY(!value->icon().pixmap(20, QIcon::Disabled).isNull());
    }
    QCOMPARE(count, 20);
    QVERIFY(!action(window, "codeAction")->icon().isNull());
    QVERIFY(!action(window, "headingMenuAction")->icon().isNull());
    QVERIFY(!action(window, "listMenuAction")->icon().isNull());
    QVERIFY(!action(window, "switchModeAction")->icon().isNull());

    Settings settings;
    settings.theme = Theme::kDark;
    QString error;
    QVERIFY2(window->applySettings(settings, &error), qPrintable(error));
    QVERIFY(!action(window, "boldAction")->icon().isNull());
    QVERIFY(!action(window, "saveAction")->icon().isNull());
    QVERIFY(dialogs.errors.empty());
  }

  void markdownToolbarActions() {
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    auto* editor = window->editor();
    editor->insertPlainText(QStringLiteral("alpha\n"));
    QVERIFY(!window->continuousMode());

    auto* toolbar =
        window->findChild<QToolBar*>(QStringLiteral("mainToolBar"));
    QVERIFY(toolbar);
    for (const auto* menuName :
         {"headingMenuAction", "listMenuAction", "switchModeAction"}) {
      auto* button = qobject_cast<QToolButton*>(
          toolbar->widgetForAction(action(window, menuName)));
      QVERIFY(button);
      QCOMPARE(button->popupMode(), QToolButton::InstantPopup);
    }

    auto* bold = action(window, "boldAction");
    QVERIFY(bold->isEnabled());
    auto cursor = editor->textCursor();
    cursor.setPosition(0);
    cursor.setPosition(5, QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    bold->trigger();
    QCOMPARE(window->session().snapshot().source,
             std::string("**alpha**\n"));
    QCOMPARE(editor->toPlainText(), QStringLiteral("**alpha**\n"));

    auto* heading = action(window, "heading2Action");
    QVERIFY(heading->isEnabled());
    cursor = editor->textCursor();
    cursor.setPosition(0);
    editor->setTextCursor(cursor);
    heading->trigger();
    QCOMPARE(window->session().snapshot().source,
             std::string("## **alpha**\n"));

    auto* unordered = action(window, "unorderedListAction");
    QVERIFY(unordered->isEnabled());
    cursor = editor->textCursor();
    cursor.setPosition(0);
    editor->setTextCursor(cursor);
    unordered->trigger();
    QCOMPARE(window->session().snapshot().source,
             std::string("- ## **alpha**\n"));
    QVERIFY(dialogs.errors.empty());
  }

  void markdownToolbarInsertions() {
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    auto* editor = window->editor();
    editor->insertPlainText(QStringLiteral("label\n"));
    auto cursor = editor->textCursor();
    cursor.setPosition(0);
    cursor.setPosition(5, QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    dialogs.linkUrl = QStringLiteral("https://example.test");
    auto* link = action(window, "linkAction");
    QVERIFY(link->isEnabled());
    link->trigger();
    QCOMPARE(window->session().snapshot().source,
             std::string("[label](https://example.test)\n"));

    auto* table = action(window, "tableAction");
    QVERIFY(table->isEnabled());
    cursor = editor->textCursor();
    cursor.setPosition(editor->toPlainText().size());
    editor->setTextCursor(cursor);
    table->trigger();
    QVERIFY(window->session().snapshot().source.find("| Column 1 | Column 2 |") !=
            std::string::npos);
    QCOMPARE(dialogs.tablePrompts, 1);

    auto* code = action(window, "codeAction");
    QVERIFY(code->isEnabled());
    cursor = editor->textCursor();
    cursor.setPosition(editor->toPlainText().size());
    editor->setTextCursor(cursor);
    code->trigger();
    QVERIFY(window->session().snapshot().source.find("```") !=
            std::string::npos);
    QVERIFY(dialogs.errors.empty());
  }

  void continuousTableRenderingAndCellEdit() {
    const QByteArray source =
        "pre\n\n| L | C | R |\n| :--- | :---: | ---: |\n"
        "| a | b | c |\n| d | e | f |\n\npost\n";
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText(QString::fromUtf8(source));
    const auto original = window->session().snapshot();
    QVERIFY(window->setContinuousMode(true));
    auto* rich = window->continuousEditor()->editor();
    const auto text = rich->document()->toRawText();
    QVERIFY(text.contains(QStringLiteral("pre")));
    QVERIFY(text.contains(QStringLiteral("L")));
    QVERIFY(text.contains(QStringLiteral("a")));
    QVERIFY(text.contains(QStringLiteral("post")));
    QVERIFY(!text.contains(QStringLiteral("| L |")));
    QTextTable* table = nullptr;
    for (auto* frame : rich->document()->rootFrame()->childFrames()) {
      if ((table = dynamic_cast<QTextTable*>(frame))) {
        break;
      }
    }
    QVERIFY(table);
    QCOMPARE(table->rows(), 3);
    QCOMPARE(table->columns(), 3);
    QCOMPARE(table->format().columnWidthConstraints().size(), 3);
    auto header = table->cellAt(0, 0).firstCursorPosition();
    QVERIFY(header.charFormat().fontWeight() >= QFont::Bold);
    QCOMPARE(table->cellAt(0, 0).firstCursorPosition().blockFormat().alignment(),
             Qt::AlignLeft);
    QCOMPARE(table->cellAt(0, 1).firstCursorPosition().blockFormat().alignment(),
             Qt::AlignHCenter);
    QCOMPARE(table->cellAt(0, 2).firstCursorPosition().blockFormat().alignment(),
             Qt::AlignRight);

    auto cursor = rich->document()->find(QStringLiteral("a"));
    QVERIFY(!cursor.isNull());
    rich->setTextCursor(cursor);
    rich->insertPlainText(QStringLiteral("x"));
    QCOMPARE(window->session().snapshot().source,
             std::string("pre\n\n| L | C | R |\n| :--- | :---: | ---: |\n"
                         "| x | b | c |\n| d | e | f |\n\npost\n"));
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);

    cursor = rich->document()->find(QStringLiteral("b"));
    QVERIFY(!cursor.isNull());
    rich->setTextCursor(cursor);
    rich->insertPlainText(QStringLiteral("b|c"));
    QCOMPARE(window->session().snapshot().source,
             std::string("pre\n\n| L | C | R |\n| :--- | :---: | ---: |\n"
                         "| a | b\\|c | c |\n| d | e | f |\n\npost\n"));
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);
    QVERIFY(window->setContinuousMode(false));
    QCOMPARE(window->session().snapshot().source, original.source);
    QVERIFY(dialogs.errors.empty());
  }

  void continuousTableAtStart() {
    const QByteArray source = "| h |\n| --- |\n| a |\n";
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText(QString::fromUtf8(source));
    const auto original = window->session().snapshot();
    QVERIFY(window->setContinuousMode(true));
    auto* rich = window->continuousEditor()->editor();
    QTextTable* table = nullptr;
    for (auto* frame : rich->document()->rootFrame()->childFrames()) {
      if ((table = dynamic_cast<QTextTable*>(frame))) {
        break;
      }
    }
    QVERIFY(table);
    QCOMPARE(table->rows(), 2);
    QCOMPARE(table->columns(), 1);
    QCOMPARE(window->session().snapshot().source, original.source);
    QVERIFY(window->setContinuousMode(false));
    QCOMPARE(window->session().snapshot().source, original.source);
  }

  void continuousTableStructureActions() {
    const QByteArray source =
        "| h | i |\n| --- | --- |\n| a | b |\n| c | d |\n";
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText(QString::fromUtf8(source));
    const auto original = window->session().snapshot();
    QVERIFY(window->setContinuousMode(true));
    auto* editor = window->continuousEditor();
    auto* rich = editor->editor();
    const auto place = [&](const QString& text) {
      auto cursor = rich->document()->find(text);
      QVERIFY(!cursor.isNull());
      rich->setTextCursor(cursor);
    };
    place(QStringLiteral("a"));
    QVERIFY(editor->tableActionEnabled(
        ContinuousEditor::TableAction::kInsertRowAbove));
    QVERIFY(editor->tableActionEnabled(
        ContinuousEditor::TableAction::kInsertRowBelow));
    QVERIFY(editor->tableActionEnabled(
        ContinuousEditor::TableAction::kRemoveRow));
    QVERIFY(editor->tableActionEnabled(
        ContinuousEditor::TableAction::kInsertColumnLeft));
    QVERIFY(editor->tableActionEnabled(
        ContinuousEditor::TableAction::kInsertColumnRight));
    QVERIFY(editor->tableActionEnabled(
        ContinuousEditor::TableAction::kRemoveColumn));
    QVERIFY(editor->tableActionEnabled(
        ContinuousEditor::TableAction::kAlignCenter));
    QVERIFY(editor->tableActionEnabled(
        ContinuousEditor::TableAction::kRemoveTable));
    auto* removeRow = action(window, "tableRemoveRowAction");
    QVERIFY(removeRow);
    QVERIFY(removeRow->isEnabled());

    editor->applyTableAction(ContinuousEditor::TableAction::kInsertRowAbove);
    QCOMPARE(window->session().snapshot().source,
             std::string("| h | i |\n| --- | --- |\n|  |  |\n"
                         "| a | b |\n| c | d |\n"));
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);

    place(QStringLiteral("a"));
    editor->applyTableAction(ContinuousEditor::TableAction::kInsertRowBelow);
    QCOMPARE(window->session().snapshot().source,
             std::string("| h | i |\n| --- | --- |\n| a | b |\n"
                         "|  |  |\n| c | d |\n"));
    window->undo();
    place(QStringLiteral("a"));
    editor->applyTableAction(ContinuousEditor::TableAction::kRemoveRow);
    QCOMPARE(window->session().snapshot().source,
             std::string("| h | i |\n| --- | --- |\n| c | d |\n"));
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);

    place(QStringLiteral("a"));
    editor->applyTableAction(ContinuousEditor::TableAction::kInsertColumnLeft);
    QCOMPARE(window->session().snapshot().source,
             std::string("|  | h | i |\n| --- | --- | --- |\n"
                         "|  | a | b |\n|  | c | d |\n"));
    window->undo();
    place(QStringLiteral("a"));
    editor->applyTableAction(ContinuousEditor::TableAction::kInsertColumnRight);
    QCOMPARE(window->session().snapshot().source,
             std::string("| h |  | i |\n| --- | --- | --- |\n"
                         "| a |  | b |\n| c |  | d |\n"));
    window->undo();
    place(QStringLiteral("b"));
    editor->applyTableAction(ContinuousEditor::TableAction::kRemoveColumn);
    QCOMPARE(window->session().snapshot().source,
             std::string("| h |\n| --- |\n| a |\n| c |\n"));
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);

    place(QStringLiteral("b"));
    editor->applyTableAction(ContinuousEditor::TableAction::kAlignLeft);
    QCOMPARE(window->session().snapshot().source,
             std::string("| h | i |\n| --- | :--- |\n| a | b |\n| c | d |\n"));
    window->undo();
    place(QStringLiteral("b"));
    editor->applyTableAction(ContinuousEditor::TableAction::kAlignCenter);
    QCOMPARE(window->session().snapshot().source,
             std::string("| h | i |\n| --- | :---: |\n| a | b |\n| c | d |\n"));
    window->undo();
    place(QStringLiteral("b"));
    editor->applyTableAction(ContinuousEditor::TableAction::kAlignRight);
    QCOMPARE(window->session().snapshot().source,
             std::string("| h | i |\n| --- | ---: |\n| a | b |\n| c | d |\n"));
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);

    place(QStringLiteral("h"));
    QVERIFY(!editor->tableActionEnabled(
        ContinuousEditor::TableAction::kInsertRowAbove));
    QVERIFY(!editor->tableActionEnabled(
        ContinuousEditor::TableAction::kRemoveRow));
    QVERIFY(editor->tableActionEnabled(
        ContinuousEditor::TableAction::kInsertRowBelow));

    editor->applyTableAction(ContinuousEditor::TableAction::kRemoveTable);
    QCOMPARE(window->session().snapshot().source, std::string{});
    QVERIFY(!removeRow->isEnabled());
    QVERIFY(!action(window, "tableInsertRowAboveAction")->isEnabled());
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);
    QVERIFY(dialogs.errors.empty());
  }

  void continuousEmptyTableCellEditing() {
    const QByteArray source =
        "| h | i |\n| --- | --- |\n|  | b |\n| c | d |\n";
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText(QString::fromUtf8(source));
    const auto original = window->session().snapshot();
    QVERIFY(window->setContinuousMode(true));
    auto* editor = window->continuousEditor();
    auto* rich = editor->editor();
    const auto table = [&]() -> QTextTable* {
      for (auto* frame : rich->document()->rootFrame()->childFrames()) {
        if (auto* value = dynamic_cast<QTextTable*>(frame)) {
          return value;
        }
      }
      return nullptr;
    };
    auto* value = table();
    QVERIFY(value);
    auto cursor = value->cellAt(1, 0).firstCursorPosition();
    rich->setTextCursor(cursor);
    rich->insertPlainText(QStringLiteral("a"));
    QCOMPARE(window->session().snapshot().source,
             std::string("| h | i |\n| --- | --- |\n| a | b |\n| c | d |\n"));
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);

    value = table();
    QVERIFY(value);
    cursor = value->cellAt(1, 0).firstCursorPosition();
    rich->setTextCursor(cursor);
    rich->insertPlainText(QStringLiteral("x|y"));
    QCOMPARE(window->session().snapshot().source,
             std::string("| h | i |\n| --- | --- |\n| x\\|y | b |\n| c | d |\n"));
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);

    value = table();
    QVERIFY(value);
    rich->setTextCursor(value->cellAt(1, 0).firstCursorPosition());
    editor->applyTableAction(ContinuousEditor::TableAction::kInsertColumnRight);
    value = table();
    QVERIFY(value);
    QCOMPARE(value->columns(), 3);
    rich->setTextCursor(value->cellAt(1, 1).firstCursorPosition());
    rich->insertPlainText(QStringLiteral("new"));
    QCOMPARE(window->session().snapshot().source,
             std::string("| h |  | i |\n| --- | --- | --- |\n"
                         "|  | new | b |\n| c |  | d |\n"));
    window->undo();
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);
    QVERIFY(dialogs.errors.empty());
  }

  void continuousTableSingleColumnGuard() {
    const QByteArray source = "| h |\n| --- |\n| a |\n";
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText(QString::fromUtf8(source));
    QVERIFY(window->setContinuousMode(true));
    auto* editor = window->continuousEditor();
    auto* rich = editor->editor();
    auto cursor = rich->document()->find(QStringLiteral("a"));
    QVERIFY(!cursor.isNull());
    rich->setTextCursor(cursor);
    QVERIFY(!editor->tableActionEnabled(
        ContinuousEditor::TableAction::kRemoveColumn));
    QVERIFY(editor->tableActionEnabled(
        ContinuousEditor::TableAction::kInsertColumnLeft));
    QVERIFY(editor->tableActionEnabled(
        ContinuousEditor::TableAction::kInsertColumnRight));
    QVERIFY(editor->tableActionEnabled(
        ContinuousEditor::TableAction::kRemoveTable));
    QCOMPARE(window->session().snapshot().source,
             std::string("| h |\n| --- |\n| a |\n"));
  }

  void continuousTableCreation() {
    const QByteArray source = "alpha\n\nbeta\n";
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText(QString::fromUtf8(source));
    const auto original = window->session().snapshot();
    auto* table = action(window, "tableAction");
    QVERIFY(table);
    QVERIFY(table->isEnabled());
    QVERIFY(window->setContinuousMode(true));
    QVERIFY(table->isEnabled());
    auto* rich = window->continuousEditor()->editor();
    auto cursor = rich->document()->find(QStringLiteral("alpha"));
    QVERIFY(!cursor.isNull());
    rich->setTextCursor(cursor);
    table->trigger();
    QCOMPARE(window->session().snapshot().source,
             std::string("alpha\n\n| Column 1 | Column 2 |\n"
                         "| --- | --- |\n|  |  |\n\nbeta\n"));
    QTextTable* value = nullptr;
    for (auto* frame : rich->document()->rootFrame()->childFrames()) {
      if ((value = dynamic_cast<QTextTable*>(frame))) {
        break;
      }
    }
    QVERIFY(value);
    rich->setTextCursor(value->cellAt(1, 0).firstCursorPosition());
    rich->insertPlainText(QStringLiteral("x"));
    QCOMPARE(window->session().snapshot().source,
             std::string("alpha\n\n| Column 1 | Column 2 |\n"
                         "| --- | --- |\n| x |  |\n\nbeta\n"));
    window->undo();
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);
    QVERIFY(window->setContinuousMode(false));
    QVERIFY(table->isEnabled());
    QVERIFY(dialogs.errors.empty());

    auto* empty = controller.newDocument();
    QVERIFY(empty->setContinuousMode(true));
    table = action(empty, "tableAction");
    QVERIFY(table->isEnabled());
    table->trigger();
    QCOMPARE(empty->session().snapshot().source,
             std::string("| Column 1 | Column 2 |\n"
                         "| --- | --- |\n|  |  |\n"));
    empty->undo();
    QCOMPARE(empty->session().snapshot().source, std::string{});
    dialogs.tableSelection = TableSelection{3, 2};
    table->trigger();
    QCOMPARE(empty->session().snapshot().source,
             std::string("| Column 1 | Column 2 | Column 3 |\n"
                         "| --- | --- | --- |\n"
                         "|  |  |  |\n|  |  |  |\n"));
    const auto sized = empty->session().snapshot().source;
    dialogs.tableSelection.reset();
    table->trigger();
    QCOMPARE(empty->session().snapshot().source, sized);
  }

  void continuousCodeAndRuleInsertion() {
    const QByteArray source = "alpha\n\nbeta\n";
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText(QString::fromUtf8(source));
    const auto original = window->session().snapshot();
    auto* code = action(window, "codeAction");
    auto* rule = action(window, "horizontalLineAction");
    QVERIFY(code);
    QVERIFY(rule);
    QVERIFY(code->isEnabled());
    QVERIFY(rule->isEnabled());
    QVERIFY(window->setContinuousMode(true));
    QVERIFY(code->isEnabled());
    QVERIFY(rule->isEnabled());
    auto* rich = window->continuousEditor()->editor();
    auto cursor = rich->document()->find(QStringLiteral("alpha"));
    QVERIFY(!cursor.isNull());
    rich->setTextCursor(cursor);
    code->trigger();
    QCOMPARE(window->session().snapshot().source,
             std::string("alpha\n\n```\n```\n\nbeta\n"));
    auto body = rich->document()->find(QStringLiteral("alpha"));
    QVERIFY(!body.isNull());
    body.movePosition(QTextCursor::NextBlock);
    rich->setTextCursor(body);
    rich->insertPlainText(QStringLiteral("x"));
    QCOMPARE(window->session().snapshot().source,
             std::string("alpha\n\n```\nx\n```\n\nbeta\n"));
    window->undo();
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);

    cursor = rich->document()->find(QStringLiteral("alpha"));
    QVERIFY(!cursor.isNull());
    rich->setTextCursor(cursor);
    rule->trigger();
    QCOMPARE(window->session().snapshot().source,
             std::string("alpha\n\n---\n\nbeta\n"));
    QVERIFY(rich->document()->toPlainText().contains(QChar(0x2500)));
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);
    QVERIFY(window->setContinuousMode(false));
    QVERIFY(code->isEnabled());
    QVERIFY(rule->isEnabled());
    QVERIFY(dialogs.errors.empty());
  }

  void continuousLinkCreation() {
    const QByteArray source = "alpha beta\n";
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText(QString::fromUtf8(source));
    const auto original = window->session().snapshot();
    auto* link = action(window, "linkAction");
    QVERIFY(link);
    QVERIFY(link->isEnabled());
    QVERIFY(window->setContinuousMode(true));
    auto* rich = window->continuousEditor()->editor();
    auto cursor = rich->document()->find(QStringLiteral("alpha"));
    QVERIFY(!cursor.isNull());
    rich->setTextCursor(cursor);
    QVERIFY(link->isEnabled());
    dialogs.linkUrl = QStringLiteral("https://example.test");
    link->trigger();
    QCOMPARE(window->session().snapshot().source,
             std::string("[alpha](<https://example.test>) beta\n"));
    QCOMPARE(dialogs.linkPrompts, 1);
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);

    cursor = rich->document()->find(QStringLiteral("beta"));
    QVERIFY(!cursor.isNull());
    rich->setTextCursor(cursor);
    QVERIFY(link->isEnabled());
    dialogs.linkUrl.reset();
    link->trigger();
    QCOMPARE(window->session().snapshot().source, original.source);
    QCOMPARE(dialogs.linkPrompts, 2);

    auto collapsed = rich->document()->find(QStringLiteral("beta"));
    collapsed.clearSelection();
    rich->setTextCursor(collapsed);
    QVERIFY(!link->isEnabled());
    QVERIFY(window->setContinuousMode(false));
    QVERIFY(link->isEnabled());
    QVERIFY(dialogs.errors.empty());
  }

  void continuousImageCreation() {
    const QByteArray source = "alpha beta\n";
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText(QString::fromUtf8(source));
    const auto original = window->session().snapshot();
    auto* image = action(window, "imageAction");
    QVERIFY(image);
    QVERIFY(image->isEnabled());
    QVERIFY(window->setContinuousMode(true));
    auto* rich = window->continuousEditor()->editor();
    auto cursor = rich->document()->find(QStringLiteral("alpha"));
    QVERIFY(!cursor.isNull());
    rich->setTextCursor(cursor);
    QVERIFY(image->isEnabled());
    dialogs.imageUrl = QStringLiteral("https://example.test/image.png");
    image->trigger();
    QCOMPARE(window->session().snapshot().source,
             std::string("![alpha](<https://example.test/image.png>) beta\n"));
    QCOMPARE(dialogs.imagePrompts, 1);
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);

    cursor = rich->document()->find(QStringLiteral("beta"));
    QVERIFY(!cursor.isNull());
    rich->setTextCursor(cursor);
    QVERIFY(image->isEnabled());
    dialogs.imageUrl.reset();
    image->trigger();
    QCOMPARE(window->session().snapshot().source, original.source);
    QCOMPARE(dialogs.imagePrompts, 2);

    auto collapsed = rich->document()->find(QStringLiteral("beta"));
    collapsed.clearSelection();
    rich->setTextCursor(collapsed);
    QVERIFY(!image->isEnabled());
    QVERIFY(window->setContinuousMode(false));
    QVERIFY(image->isEnabled());
    QVERIFY(dialogs.errors.empty());
  }

  void continuousInlineCodeEditing() {
    const QByteArray source = "`old`\n";
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText(QString::fromUtf8(source));
    const auto original = window->session().snapshot();
    QVERIFY(window->setContinuousMode(true));
    auto* rich = window->continuousEditor()->editor();
    QVERIFY(!rich->document()->toRawText().contains(
        QStringLiteral("Read-only: code")));
    auto cursor = rich->document()->find(QStringLiteral("old"));
    QVERIFY(!cursor.isNull());
    rich->setTextCursor(cursor);
    rich->insertPlainText(QStringLiteral("new"));
    QCOMPARE(window->session().snapshot().source, std::string("`new`\n"));
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);
    QVERIFY(window->setContinuousMode(false));
    QCOMPARE(window->session().snapshot().source, original.source);
    QVERIFY(dialogs.errors.empty());
  }

  void continuousDarkThemeColors() {
    const QByteArray source =
        "alpha\n\n```\ncode\n```\n\n| h |\n| --- |\n| a |\n";
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText(QString::fromUtf8(source));
    QVERIFY(window->setContinuousMode(true));
    Settings settings;
    settings.theme = Theme::kDark;
    QString error;
    QVERIFY2(window->applySettings(settings, &error), qPrintable(error));
    QVERIFY(!qApp->styleSheet().isEmpty());
    auto* rich = window->continuousEditor()->editor();
    auto cursor = rich->document()->find(QStringLiteral("alpha"));
    QVERIFY(!cursor.isNull());
    QVERIFY(!cursor.charFormat().hasProperty(QTextFormat::BackgroundBrush));
    cursor = rich->document()->find(QStringLiteral("code"));
    QVERIFY(!cursor.isNull());
    QVERIFY(cursor.charFormat().background().color().lightness() < 128);
    QVERIFY(rich->document()
                ->findBlock(cursor.position())
                .blockFormat()
                .hasProperty(QTextFormat::BackgroundBrush));
    QTextTable* table = nullptr;
    for (auto* frame : rich->document()->rootFrame()->childFrames()) {
      if ((table = dynamic_cast<QTextTable*>(frame))) {
        break;
      }
    }
    QVERIFY(table);
    QVERIFY(table->cellAt(0, 0)
                .format()
                .background()
                .color()
                .lightness() < 128);
    settings.theme = Theme::kLight;
    QVERIFY2(window->applySettings(settings, &error), qPrintable(error));
    QVERIFY(qApp->styleSheet().isEmpty());
    QVERIFY(dialogs.errors.empty());
  }

  void continuousTypographySettings() {
    const QByteArray source =
        "# Heading\n\nparagraph\n\n> quote\n\n```\ncode\n```\n\n`inline`\n";
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText(QString::fromUtf8(source));
    QVERIFY(window->setContinuousMode(true));
    Settings settings;
    settings.wysiwyg.bodyFontSize = 18;
    settings.wysiwyg.headingBold = false;
    settings.wysiwyg.headingSizes = {40, 32, 26, 21, 18, 15};
    settings.wysiwyg.codeFontSize = 19;
    settings.wysiwyg.quoteFontSize = 17;
    settings.wysiwyg.quoteItalic = true;
    QString error;
    QVERIFY2(window->applySettings(settings, &error), qPrintable(error));
    auto* rich = window->continuousEditor()->editor();
    QCOMPARE(rich->font().pointSize(), 18);
    auto cursor = rich->document()->find(QStringLiteral("Heading"));
    QVERIFY(!cursor.isNull());
    QCOMPARE(cursor.charFormat().fontPointSize(), 40.0);
    QVERIFY(cursor.charFormat().fontWeight() < QFont::Bold);
    cursor = rich->document()->find(QStringLiteral("quote"));
    QVERIFY(!cursor.isNull());
    QCOMPARE(cursor.charFormat().fontPointSize(), 17.0);
    QVERIFY(cursor.charFormat().fontItalic());
    cursor = rich->document()->find(QStringLiteral("code"));
    QVERIFY(!cursor.isNull());
    QCOMPARE(cursor.charFormat().fontPointSize(), 19.0);
    cursor = rich->document()->find(QStringLiteral("inline"));
    QVERIFY(!cursor.isNull());
    QCOMPARE(cursor.charFormat().fontPointSize(), 19.0);
    QVERIFY(window->setPagedMode(true));
    cursor = rich->document()->find(QStringLiteral("Heading"));
    QVERIFY(!cursor.isNull());
    QCOMPARE(cursor.charFormat().fontPointSize(), 40.0);
    QVERIFY(dialogs.errors.empty());
  }

  void continuousAcceptanceMatrix() {
    const QByteArray source =
        "# Heading\n\n"
        "Paragraph **bold** *italic* ~~strike~~ `inline`.\n\n"
        "> quote\n\n"
        "- one\n- two\n\n"
        "```js\ncode\n```\n\n"
        "[link](https://example.test)\n"
        "![alt](https://example.test/image.png)\n\n"
        "| h | i |\n| --- | --- |\n| a | b |\n\n"
        "---\n\n"
        "<script>bad()</script>\n\n"
        "> | nested |\n> | --- |\n> | x |\n";
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText(QString::fromUtf8(source));
    const auto initial = window->session().snapshot();
    const bool initialDirty = window->session().dirty();
    QVERIFY(window->setContinuousMode(true));
    auto* rich = window->continuousEditor()->editor();
    const auto text = rich->document()->toRawText();
    QVERIFY(text.contains(QStringLiteral("Heading")));
    QVERIFY(text.contains(QStringLiteral("bold")));
    QVERIFY(text.contains(QStringLiteral("italic")));
    QVERIFY(text.contains(QStringLiteral("strike")));
    QVERIFY(text.contains(QStringLiteral("inline")));
    QVERIFY(text.contains(QStringLiteral("quote")));
    QVERIFY(text.contains(QStringLiteral("one")));
    QVERIFY(text.contains(QStringLiteral("two")));
    QVERIFY(text.contains(QStringLiteral("code")));
    QVERIFY(text.contains(QStringLiteral("link")));
    QVERIFY(text.contains(QStringLiteral("alt")));
    QVERIFY(text.contains(QStringLiteral("a")));
    QVERIFY(text.contains(QStringLiteral("b")));
    QVERIFY(text.contains(QChar(0x2500)));
    QVERIFY(text.contains(QStringLiteral("[Read-only: HTML")));
    QVERIFY(text.contains(QStringLiteral("table - edit in Markdown")));
    QVERIFY(!text.contains(QStringLiteral("Read-only: code]")));
    int tables = 0;
    for (auto* frame : rich->document()->rootFrame()->childFrames()) {
      if (dynamic_cast<QTextTable*>(frame)) {
        ++tables;
      }
    }
    QCOMPARE(tables, 1);
    QCOMPARE(window->session().snapshot().source, initial.source);
    QCOMPARE(window->session().snapshot().token, initial.token);
    QVERIFY(window->setContinuousMode(false));
    QCOMPARE(window->session().snapshot().source, initial.source);
    QCOMPARE(window->session().snapshot().token, initial.token);
    QVERIFY(window->setContinuousMode(true));
    QCOMPARE(window->session().snapshot().source, initial.source);
    QCOMPARE(window->session().snapshot().token, initial.token);
    QCOMPARE(window->session().dirty(), initialDirty);
    QVERIFY(dialogs.errors.empty());
  }

  void continuousUnsupportedTableFallback() {
    const QByteArray source = "> | h |\n> | --- |\n> | a |\n";
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText(QString::fromUtf8(source));
    const auto original = window->session().snapshot();
    QVERIFY(window->setContinuousMode(true));
    auto* rich = window->continuousEditor()->editor();
    QVERIFY(rich->document()->toRawText().contains(
        QStringLiteral("table - edit in Markdown")));
    QCOMPARE(window->session().snapshot().source, original.source);
  }

  void continuousImageAltLabels() {
    const QByteArray source =
        "![before](/image \"T\")\n\n"
        "![ref][id]\n\n"
        "[id]: /img \"I\"\n\n"
        "[![logo](file:///private)](/keep)\n\n"
        "![unsafe](javascript:ignored)\n";
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText(QString::fromUtf8(source));
    const auto original = window->session().snapshot();
    const bool originalDirty = window->session().dirty();
    QVERIFY(window->setContinuousMode(true));
    auto* rich = window->continuousEditor()->editor();
    const auto replace = [&](const QString& label, const QString& replacement) {
      auto cursor = rich->document()->find(label);
      QVERIFY(!cursor.isNull());
      QVERIFY(!cursor.charFormat().isImageFormat());
      QVERIFY(cursor.charFormat().anchorHref().isEmpty());
      rich->setTextCursor(cursor);
      rich->insertPlainText(replacement);
    };
    replace(QStringLiteral("before"), QStringLiteral("after"));
    replace(QStringLiteral("ref"), QStringLiteral("ref2"));
    replace(QStringLiteral("logo"), QStringLiteral("brand"));
    replace(QStringLiteral("unsafe"), QStringLiteral("safe"));
    const QByteArray expected =
        "![after](/image \"T\")\n\n"
        "![ref2][id]\n\n"
        "[id]: /img \"I\"\n\n"
        "[![brand](file:///private)](/keep)\n\n"
        "![safe](javascript:ignored)\n";
    QCOMPARE(window->session().snapshot().source, expected.toStdString());
    QVERIFY(window->session().dirty());
    for (int i = 0; i < 4; ++i) {
      window->undo();
    }
    QCOMPARE(window->session().snapshot().source, original.source);
    QCOMPARE(window->session().dirty(), originalDirty);
    QVERIFY(dialogs.errors.empty());
  }

  void continuousStructuralCommands() {
    const QByteArray source =
        "alpha\n\nbeta\n\ngamma\n\n- one\n- two\n";
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText(QString::fromUtf8(source));
    const auto original = window->session().snapshot();
    const bool originalDirty = window->session().dirty();
    QVERIFY(window->setContinuousMode(true));
    auto* rich = window->continuousEditor()->editor();
    auto* quote = action(window, "blockquoteAction");
    auto* ordered = action(window, "orderedListAction");
    auto* unordered = action(window, "unorderedListAction");
    QVERIFY(quote);
    QVERIFY(ordered);
    QVERIFY(unordered);
    const auto place = [&](const QString& text) {
      auto cursor = rich->document()->find(text);
      QVERIFY(!cursor.isNull());
      rich->setTextCursor(cursor);
    };

    place(QStringLiteral("alpha"));
    QVERIFY(quote->isEnabled());
    QVERIFY(ordered->isEnabled());
    QVERIFY(unordered->isEnabled());
    quote->trigger();
    QCOMPARE(window->session().snapshot().source,
             std::string("> alpha\n\nbeta\n\ngamma\n\n- one\n- two\n"));
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);

    place(QStringLiteral("alpha"));
    QVERIFY(quote->isEnabled());
    quote->trigger();
    place(QStringLiteral("alpha"));
    QVERIFY(quote->isEnabled());
    quote->trigger();
    QCOMPARE(window->session().snapshot().source, original.source);
    window->undo();
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);

    place(QStringLiteral("beta"));
    QVERIFY(ordered->isEnabled());
    ordered->trigger();
    QCOMPARE(window->session().snapshot().source,
             std::string("alpha\n\n1. beta\n\ngamma\n\n- one\n- two\n"));
    place(QStringLiteral("beta"));
    QVERIFY(unordered->isEnabled());
    unordered->trigger();
    QCOMPARE(window->session().snapshot().source,
             std::string("alpha\n\n- beta\n\ngamma\n\n- one\n- two\n"));
    window->undo();
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);

    place(QStringLiteral("one"));
    QVERIFY(ordered->isEnabled());
    ordered->trigger();
    QCOMPARE(window->session().snapshot().source,
             std::string("alpha\n\nbeta\n\ngamma\n\n1. one\n2. two\n"));
    place(QStringLiteral("one"));
    QVERIFY(unordered->isEnabled());
    unordered->trigger();
    QCOMPARE(window->session().snapshot().source,
             std::string("alpha\n\nbeta\n\ngamma\n\n- one\n- two\n"));
    window->undo();
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);

    place(QStringLiteral("one"));
    QVERIFY(quote->isEnabled());
    quote->trigger();
    QCOMPARE(window->session().snapshot().source,
             std::string("alpha\n\nbeta\n\ngamma\n\n> - one\n> - two\n"));
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);

    rich->selectAll();
    QVERIFY(!quote->isEnabled());
    QVERIFY(!ordered->isEnabled());
    QVERIFY(!unordered->isEnabled());
    QCOMPARE(window->session().snapshot().source, original.source);
    QVERIFY(window->setContinuousMode(false));
    QVERIFY(quote->isEnabled());
    QVERIFY(ordered->isEnabled());
    QVERIFY(unordered->isEnabled());
    QCOMPARE(window->session().dirty(), originalDirty);
    QVERIFY(dialogs.errors.empty());
  }

  void continuousCodeBlockQuote() {
    const QByteArray source = "```\ncode\n```\n";
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText(QString::fromUtf8(source));
    const auto original = window->session().snapshot();
    QVERIFY(window->setContinuousMode(true));
    auto* rich = window->continuousEditor()->editor();
    auto* quote = action(window, "blockquoteAction");
    QVERIFY(quote);
    auto cursor = rich->document()->find(QStringLiteral("code"));
    QVERIFY(!cursor.isNull());
    rich->setTextCursor(cursor);
    QVERIFY(quote->isEnabled());
    quote->trigger();
    QCOMPARE(window->session().snapshot().source,
             std::string("> ```\n> code\n> ```\n"));
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);
    QVERIFY(dialogs.errors.empty());
  }

  void continuousCodeBlockEditing() {
    const QByteArray source =
        "before\n\n```js\nold\nline2\n```\n\nafter\n";
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    window->editor()->insertPlainText(QString::fromUtf8(source));
    const auto original = window->session().snapshot();
    QVERIFY(window->setContinuousMode(true));
    auto* rich = window->continuousEditor()->editor();
    const auto displayed = rich->document()->toRawText();
    QVERIFY(displayed.contains(QStringLiteral("old")));
    QVERIFY(displayed.contains(QStringLiteral("line2")));
    QVERIFY(!displayed.contains(QStringLiteral("```")));

    auto cursor = rich->document()->find(QStringLiteral("old"));
    QVERIFY(!cursor.isNull());
    rich->setTextCursor(cursor);
    rich->insertPlainText(QStringLiteral("new"));
    QCOMPARE(window->session().snapshot().source,
             std::string("before\n\n```js\nnew\nline2\n```\n\nafter\n"));
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);

    cursor = rich->document()->find(QStringLiteral("line2"));
    QVERIFY(!cursor.isNull());
    cursor.setPosition(cursor.selectionEnd());
    rich->setTextCursor(cursor);
    rich->insertPlainText(QStringLiteral("\nadded"));
    QCOMPARE(window->session().snapshot().source,
             std::string("before\n\n```js\nold\nline2\nadded\n```\n\nafter\n"));
    window->undo();
    QCOMPARE(window->session().snapshot().source, original.source);
    QVERIFY(dialogs.errors.empty());

    cursor = rich->document()->find(QStringLiteral("old"));
    QVERIFY(!cursor.isNull());
    rich->setTextCursor(cursor);
    rich->insertPlainText(QStringLiteral("```"));
    QCOMPARE(window->session().snapshot().source, original.source);
    QVERIFY(!dialogs.errors.empty());
    QVERIFY(window->setContinuousMode(false));
    QCOMPARE(window->session().snapshot().source, original.source);
    QVERIFY(window->setContinuousMode(true));
    QCOMPARE(window->session().snapshot().source, original.source);
  }

  void settingsAppliedToExistingWindow() {
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    Settings settings;
    settings.theme = Theme::kLight;
    settings.editorFontSize = 17;
    settings.wordWrap = false;
    settings.defaultFileLocation = QDir::tempPath();
    settings.shortcuts.insert(QStringLiteral("boldAction"),
                              QStringLiteral("Ctrl+Alt+B"));
    QString error;
    QVERIFY2(window->applySettings(settings, &error), qPrintable(error));
    QCOMPARE(window->editor()->font().pointSize(), 17);
    QCOMPARE(window->editor()->lineWrapMode(), QPlainTextEdit::NoWrap);
    QCOMPARE(window->continuousEditor()->editor()->lineWrapMode(),
             QTextEdit::NoWrap);
    auto* bold = action(window, "boldAction");
    QVERIFY(bold);
    QCOMPARE(bold->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+B")));
    QCOMPARE(dialogs.defaultLocation(), QDir::tempPath());
    QApplication::setPalette(QApplication::style()->standardPalette());
  }

  void continuousRejectedCommandsAndNoop() {
    TestDialogs dialogs;
    ApplicationController controller(dialogs);
    auto* window = controller.newDocument();
    QVERIFY(window->setContinuousMode(true));
    auto* rich = window->continuousEditor()->editor();
    QVERIFY(rich->toPlainText().contains("Read-only"));
    const auto empty = window->session().snapshot();
    rich->insertPlainText("not mapped");
    QCOMPARE(window->session().snapshot().source, std::string{});
    QCOMPARE(window->session().snapshot().token, empty.token);
    QVERIFY(!dialogs.errors.empty());
    window->setContinuousMode(false);
    window->editor()->insertPlainText("word\n");
    window->setContinuousMode(true);
    const auto before = window->session().snapshot();
    const auto node = window->continuousEditor()->headingNode();
    QVERIFY(node);
    const auto errors = dialogs.errors.size();
    QVERIFY(window->applyContinuousCommand(
        before.token, markdown::SetHeadingLevel{*node, 0}));
    QCOMPARE(window->session().snapshot().token, before.token);
    QCOMPARE(window->session().snapshot().source, before.source);
    QCOMPARE(dialogs.errors.size(), errors);
    QVERIFY(!window->applyContinuousCommand(
        before.token, markdown::SetHeadingLevel{*node, 7}));
    QCOMPARE(window->session().snapshot().token, before.token);
    QCOMPARE(window->session().snapshot().source, before.source);
    window->activateWindow();
    rich->setFocus();
    QCoreApplication::processEvents();
    QTest::keySequence(rich, QKeySequence(QKeySequence::Bold));
    QVERIFY(dialogs.errors.size() > errors);
    QCOMPARE(window->session().snapshot().source, before.source);
    auto cursor = rich->document()->find("word");
    rich->setTextCursor(cursor);
    rich->insertPlainText("word");
    QCOMPARE(window->session().snapshot().token, before.token);
    QTest::keySequence(rich, QKeySequence(QKeySequence::Undo));
    QCOMPARE(window->session().snapshot().source, std::string{});
    QTest::keySequence(rich, QKeySequence(QKeySequence::Redo));
    QCOMPARE(window->session().snapshot().source, before.source);
  }
};

}  // namespace markpad

QTEST_MAIN(markpad::MainWindowSmokeTest)

#include "smoke_test.moc"
