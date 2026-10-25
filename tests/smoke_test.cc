#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileOpenEvent>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextLayout>
#include <QtTest/QtTest>

#include "application_controller.h"

namespace markpad {
namespace {

class TestDialogs final : public DocumentDialogs {
 public:
  QString openPath;
  std::optional<SaveSelection> saveSelection;
  std::optional<Encoding> encoding;
  CloseChoice closeChoice = CloseChoice::kCancel;
  QStringList errors;
  int closePrompts = 0;
  QString chooseOpen(QWidget*) override { return openPath; }
  std::optional<SaveSelection> chooseSave(QWidget*, const QString&,
                                          FileFormat) override {
    return saveSelection;
  }
  std::optional<Encoding> chooseEncoding(QWidget*) override { return encoding; }
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
};

}  // namespace markpad

QTEST_MAIN(markpad::MainWindowSmokeTest)

#include "smoke_test.moc"
