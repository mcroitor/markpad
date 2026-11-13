#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QAbstractTextDocumentLayout>
#include <QImage>
#include <QPainter>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextEdit>
#include <QTextTable>
#include <QVBoxLayout>
#include <QtTest/QtTest>
#include <algorithm>
#include <cmath>

#include "document_session.h"
#include "markdown_document.h"
#include "continuous_editor.h"
#include "canvas_edit.h"

namespace {
class NoFiles final : public markpad::DocumentFileService {
 public:
  markpad::ReadResult read(const std::filesystem::path&,
                           std::optional<markpad::Encoding>) override { return {}; }
  markpad::DocumentStatus write(const std::filesystem::path&,
                                 const markpad::FileContents&) override { return {}; }
};

// Narrow spike helper: computes a single replacement span for one text run.
// The boundaries never land inside a UTF-16 surrogate pair.
struct ProxyChange {
  int prefix = 0;
  int suffix = 0;
};

ProxyChange proxyChange(const QString& previous, const QString& current) {
  ProxyChange change;
  while (change.prefix < current.size() &&
         change.prefix < previous.size() &&
         current[change.prefix] == previous[change.prefix]) {
    ++change.prefix;
  }
  if (change.prefix > 0 && change.prefix < current.size() &&
      current[change.prefix].isLowSurrogate() &&
      current[change.prefix - 1].isHighSurrogate()) {
    --change.prefix;
  }
  while (change.suffix < current.size() - change.prefix &&
         change.suffix < previous.size() - change.prefix &&
         current[current.size() - change.suffix - 1] ==
             previous[previous.size() - change.suffix - 1]) {
    ++change.suffix;
  }
  const qsizetype suffixStart = current.size() - change.suffix;
  if (change.suffix > 0 && suffixStart > 0 && suffixStart < current.size() &&
      current[suffixStart].isLowSurrogate() &&
      current[suffixStart - 1].isHighSurrogate()) {
    --change.suffix;
  }
  return change;
}

// A limited boundary navigation experiment. This handles only unmodified Left

// and Right keys; selections and other navigation remain unsupported.
class BoundaryNavigator final : public QObject {
 public:
  BoundaryNavigator(QTextEdit* first, QTextEdit* second)
      : first_(first), second_(second) {
    first_->installEventFilter(this);
    second_->installEventFilter(this);
  }

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() != QEvent::KeyPress) return false;
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->modifiers() != Qt::NoModifier) return false;
    auto* current = qobject_cast<QTextEdit*>(watched);
    if (!current || current->textCursor().hasSelection()) return false;
    QTextEdit* target = nullptr;
    QTextCursor::MoveOperation position = QTextCursor::Start;
    if (current == first_ && key->key() == Qt::Key_Right &&
        current->textCursor().atEnd()) {
      target = second_;
    } else if (current == second_ && key->key() == Qt::Key_Left &&
               current->textCursor().atStart()) {
      target = first_;
      position = QTextCursor::End;
    }
    if (!target) return false;
    target->moveCursor(position);
    target->setFocus(Qt::OtherFocusReason);
    return true;
  }

 private:
  QTextEdit* first_;
  QTextEdit* second_;
};

// Deliberately restricted to two single-line plain paragraphs. The two Qt
// documents are projections; the DocumentSession owns the canonical source.
class Prototype {
 public:
  Prototype() : session(files) {
    session.replaceSource(session.snapshot().token, "first\n\nsecond\n");
    for (auto& page : pages) {
      page.setAcceptRichText(false);
      page.setUndoRedoEnabled(false);
      QObject::connect(&page, &QTextEdit::textChanged, [&] { changed(); });
    }
    render();
  }

  void render() {
    const auto source = QString::fromStdString(session.snapshot().source);
    const auto parts = source.trimmed().split(QStringLiteral("\n\n"));
    for (int i = 0; i < 2; ++i) {
      const QSignalBlocker blocker(&pages[i]);
      pages[i].setPlainText(parts.value(i));
    }
  }

  void changed() {
    const auto snap = session.snapshot();
    auto parsed = markpad::markdown::parse(QByteArray::fromStdString(snap.source),
                                           snap.token.revision);
    if (!parsed.document) return;
    int changedPage = -1;
    for (int i = 0; i < 2; ++i) {
      if (pages[i].toPlainText() != previous[i]) changedPage = i;
    }
    if (changedPage < 0) return;
    std::vector<size_t> texts;
    for (size_t i = 0; i < parsed.document->nodes.size(); ++i) {
      const auto& node = parsed.document->nodes[i];
      if (node.kind == markpad::markdown::NodeKind::kText && node.source)
        texts.push_back(i);
    }
    if (texts.size() != 2) { render(); return; }
    auto result = markpad::markdown::applyEdit(
        *parsed.document, markpad::markdown::snapshotToken(*parsed.document),
        markpad::markdown::ReplaceText{texts[size_t(changedPage)],
                                       pages[changedPage].toPlainText().toUtf8()});
    if (!result.document || session.replaceSource(snap.token, result.document->source.toStdString())) {
      render();
      return;
    }
    history.push_back(snap.source);
    previous[changedPage] = pages[changedPage].toPlainText();
  }

  void initialize() {
    for (int i = 0; i < 2; ++i) previous[i] = pages[i].toPlainText();
  }
  // Only two independent plain-text blocks; not production page geometry.
  void reflow(qreal contentHeight, qreal contentWidth = 200) {
    const auto cursor = pages[1].textCursor();
    qreal heights[2];
    for (int i = 0; i < 2; ++i) {
      pages[i].document()->setTextWidth(contentWidth);
      heights[i] = pages[i].document()->size().height();
    }
    layout0.addWidget(&pages[0]);
    (heights[0] + heights[1] <= contentHeight ? layout0 : layout1)
        .addWidget(&pages[1]);
    pages[1].setTextCursor(cursor);
  }
  void undo() {
    if (history.empty()) return;
    session.replaceSource(session.snapshot().token, history.back());
    history.pop_back();
    render();
    initialize();
  }

  NoFiles files;
  markpad::DocumentSession session;
  QWidget sheets[2];
  QVBoxLayout layout0{&sheets[0]};
  QVBoxLayout layout1{&sheets[1]};
  QTextEdit pages[2];
  QString previous[2];
  std::vector<std::string> history;
};

class PagesTest : public QObject {
  Q_OBJECT
 private slots:
  void secondPageAndUndo() {
    Prototype p;
    p.initialize();
    p.pages[1].moveCursor(QTextCursor::End);
    p.pages[1].insertPlainText(QStringLiteral("x"));
    QCOMPARE(p.session.snapshot().source, std::string("first\n\nsecondx\n"));
    p.undo();
    QCOMPARE(p.session.snapshot().source, std::string("first\n\nsecond\n"));
    QCOMPARE(p.pages[1].toPlainText(), QStringLiteral("second"));
  }

  void imeCommit() {
    Prototype p;
    p.initialize();
    p.pages[1].moveCursor(QTextCursor::End);
    QInputMethodEvent event;
    event.setCommitString(QString::fromUtf8("é"));
    QApplication::sendEvent(&p.pages[1], &event);
    QCOMPARE(p.session.snapshot().source, std::string("first\n\nsecond\xc3\xa9\n"));
  }

  void imePreeditDoesNotCommit() {
    Prototype p;
    p.initialize();
    p.pages[1].moveCursor(QTextCursor::End);
    QInputMethodEvent event(QStringLiteral("compose"), {});
    QApplication::sendEvent(&p.pages[1], &event);
    QCOMPARE(p.session.snapshot().source, std::string("first\n\nsecond\n"));
    QCOMPARE(p.history.size(), size_t(0));
  }

  void syntheticImeCommitAfterReflow() {
    Prototype p;
    p.initialize();
    p.reflow(1);
    p.pages[1].moveCursor(QTextCursor::End);
    QInputMethodEvent preedit(QStringLiteral("compose"), {});
    QApplication::sendEvent(&p.pages[1], &preedit);
    p.reflow(1000);
    QInputMethodEvent commit;
    commit.setCommitString(QString::fromUtf8("é"));
    QApplication::sendEvent(&p.pages[1], &commit);
    QCOMPARE(p.session.snapshot().source,
             std::string("first\n\nsecond\xc3\xa9\n"));
  }

  void staleSessionTokenRejected() {
    Prototype p;
    p.initialize();
    const auto stale = p.session.snapshot();
    p.pages[1].moveCursor(QTextCursor::End);
    p.pages[1].insertPlainText(QStringLiteral("x"));
    const auto error = p.session.replaceSource(stale.token, "stale\n");
    QVERIFY(error.has_value());
    QCOMPARE(p.session.snapshot().source, std::string("first\n\nsecondx\n"));
  }

  void measuredReflowPreservesCursor() {
    Prototype p;
    p.initialize();
    p.reflow(1);
    QCOMPARE(p.pages[1].parentWidget(), &p.sheets[1]);
    p.pages[1].moveCursor(QTextCursor::End);
    p.reflow(1000);
    QCOMPARE(p.pages[1].parentWidget(), &p.sheets[0]);
    QCOMPARE(p.pages[1].textCursor().position(), 6);
    p.pages[1].insertPlainText(QStringLiteral("x"));
    QCOMPARE(p.session.snapshot().source, std::string("first\n\nsecondx\n"));
    p.undo();
    QCOMPARE(p.session.snapshot().source, std::string("first\n\nsecond\n"));
  }

  void widthChangeReflowsWithoutChangingSource() {
    Prototype p;
    p.initialize();
    p.pages[0].moveCursor(QTextCursor::End);
    p.pages[0].insertPlainText(
        QStringLiteral(" alpha beta gamma delta epsilon zeta eta theta"));
    const auto source = p.session.snapshot();
    p.pages[1].moveCursor(QTextCursor::End);
    p.pages[0].document()->setTextWidth(500);
    p.pages[1].document()->setTextWidth(500);
    const auto wide = p.pages[0].document()->size().height() +
                      p.pages[1].document()->size().height();
    p.pages[0].document()->setTextWidth(60);
    p.pages[1].document()->setTextWidth(60);
    const auto narrow = p.pages[0].document()->size().height() +
                        p.pages[1].document()->size().height();
    QVERIFY(narrow > wide);
    const auto limit = (wide + narrow) / 2;
    p.reflow(limit, 500);
    QCOMPARE(p.pages[1].parentWidget(), &p.sheets[0]);
    p.reflow(limit, 60);
    QCOMPARE(p.pages[1].parentWidget(), &p.sheets[1]);
    QCOMPARE(p.pages[1].textCursor().position(), 6);
    QCOMPARE(p.session.snapshot().source, source.source);
    QCOMPARE(p.session.snapshot().token, source.token);
  }

  void visibleSheetsContainMeasuredTextWidgets() {
    QWidget host;
    QVBoxLayout hostLayout(&host);
    Prototype p;
    p.initialize();
    for (int i = 0; i < 2; ++i) {
      p.sheets[i].setFixedSize(240, 120);
      hostLayout.addWidget(&p.sheets[i]);
      p.pages[i].setFixedWidth(200);
      p.pages[i].document()->setTextWidth(196);
      const int height = int(std::ceil(p.pages[i].document()->size().height())) +
                         2 * p.pages[i].frameWidth();
      p.pages[i].setFixedHeight(height);
    }
    p.reflow(1, 196);
    host.show();
    QApplication::processEvents();
    for (int i = 0; i < 2; ++i) {
      QVERIFY(p.sheets[i].isVisible());
      QVERIFY(p.pages[i].isVisible());
      QCOMPARE(p.pages[i].parentWidget(), &p.sheets[i]);
      QVERIFY(p.sheets[i].rect().contains(p.pages[i].geometry()));
    }
  }

  void independentEditorsDoNotExtendSelection() {
    Prototype p;
    p.initialize();
    p.reflow(1);
    p.pages[0].moveCursor(QTextCursor::End);
    QTest::keyClick(&p.pages[0], Qt::Key_Right, Qt::ShiftModifier);
    QVERIFY(!p.pages[0].textCursor().hasSelection());
    QVERIFY(!p.pages[1].textCursor().hasSelection());
  }

  void boundaryNavigationAndEdit() {
    QWidget host;
    QVBoxLayout hostLayout(&host);
    Prototype p;
    p.initialize();
    p.reflow(1);
    hostLayout.addWidget(&p.sheets[0]);
    hostLayout.addWidget(&p.sheets[1]);
    BoundaryNavigator navigator(&p.pages[0], &p.pages[1]);
    host.show();
    host.activateWindow();
    QApplication::processEvents();
    p.pages[0].setFocus();
    QApplication::processEvents();
    p.pages[0].moveCursor(QTextCursor::End);
    QTest::keyClick(&p.pages[0], Qt::Key_Right);
    QCOMPARE(p.pages[1].textCursor().position(), 0);
    QVERIFY(p.pages[1].hasFocus());
    QTest::keyClicks(&p.pages[1], "x");
    QCOMPARE(p.session.snapshot().source, std::string("first\n\nxsecond\n"));
    p.pages[1].moveCursor(QTextCursor::Start);
    QTest::keyClick(&p.pages[1], Qt::Key_Left);
    QVERIFY(p.pages[0].hasFocus());
    QCOMPARE(p.pages[0].textCursor().position(), 5);
  }

  void logicalSelectionAcrossTwoDocuments() {
    Prototype p;
    p.initialize();
    p.reflow(1);
    auto first = p.pages[0].textCursor();
    first.setPosition(2);
    first.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    p.pages[0].setTextCursor(first);
    auto second = p.pages[1].textCursor();
    second.setPosition(0);
    second.setPosition(3, QTextCursor::KeepAnchor);
    p.pages[1].setTextCursor(second);
    QCOMPARE(p.pages[0].textCursor().selectedText(), QStringLiteral("rst"));
    QCOMPARE(p.pages[1].textCursor().selectedText(), QStringLiteral("sec"));
    QCOMPARE(p.session.snapshot().source, std::string("first\n\nsecond\n"));
  }

  void singleDocumentPageCoordinatesAndSelection() {
    QTextDocument document;
    document.setPageSize(QSizeF(200, 100));
    QString content;
    for (int i = 0; i < 30; ++i) {
      content += QStringLiteral("paragraph %1\n").arg(i);
    }
    document.setPlainText(content);
    QVERIFY(document.pageCount() > 1);
    auto* layout = document.documentLayout();
    QTextBlock firstOnSecondPage;
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
      if (layout->blockBoundingRect(block).top() >= 100) {
        firstOnSecondPage = block;
        break;
      }
    }
    QVERIFY(firstOnSecondPage.isValid());
    const auto rect = layout->blockBoundingRect(firstOnSecondPage);
    const QPointF pagePoint = rect.topLeft() + QPointF(2, 2) - QPointF(0, 100);
    const int hit = layout->hitTest(pagePoint + QPointF(0, 100), Qt::FuzzyHit);
    QVERIFY(hit >= firstOnSecondPage.position());
    QVERIFY(hit < firstOnSecondPage.position() + firstOnSecondPage.length());
    QTextCursor cursor(&document);
    cursor.setPosition(0);
    cursor.setPosition(firstOnSecondPage.position() + 1, QTextCursor::KeepAnchor);
    QVERIFY(cursor.hasSelection());
    QVERIFY(cursor.selectedText().contains(QStringLiteral("paragraph 0")));
    QVERIFY(cursor.selectedText().contains(firstOnSecondPage.text().left(1)));

    QImage page(200, 100, QImage::Format_ARGB32_Premultiplied);
    page.fill(Qt::white);
    QPainter painter(&page);
    painter.translate(0, -100);
    document.drawContents(&painter, QRectF(0, 100, 200, 100));
    painter.end();
    bool painted = false;
    for (int y = 0; y < page.height() && !painted; ++y) {
      for (int x = 0; x < page.width(); ++x) {
        if (page.pixelColor(x, y) != QColor(Qt::white)) { painted = true; break; }
      }
    }
    QVERIFY(painted);
  }

  void unicodeProxyChangeKeepsSurrogatePairs() {
    const auto replaced = proxyChange(QString::fromUtf8("\xf0\x9f\x98\x80"),
                                      QString::fromUtf8("\xf0\x9f\x98\x81"));
    QCOMPARE(replaced.prefix, 0);
    QCOMPARE(replaced.suffix, 0);
    const auto appended = proxyChange(QString::fromUtf8("\xf0\x9f\x98\x80 x"),
                                      QString::fromUtf8("\xf0\x9f\x98\x80 x!"));
    QCOMPARE(appended.prefix, 4);
    QCOMPARE(appended.suffix, 0);
    const auto removed = proxyChange(QString::fromUtf8("a\xf0\x9f\x98\x80"),
                                     QStringLiteral("a"));
    QCOMPARE(removed.prefix, 1);
    QCOMPARE(removed.suffix, 0);
  }

  void productionMappingAndVersionedCommand() {
    NoFiles files;
    markpad::DocumentSession session(files);
    QVERIFY(!session.replaceSource(session.snapshot().token,
                                   "first\n\nsecond\n"));
    markpad::ContinuousEditor editor;
    QVERIFY(editor.render(session.snapshot()).isEmpty());
    bool exact = false;
    const int view = editor.viewPosition(9, &exact);
    QVERIFY(exact);
    QCOMPARE(editor.sourcePosition(view).value(), qsizetype(9));
    int commands = 0;
    QObject::connect(&editor, &markpad::ContinuousEditor::commandRequested,
                     [&](const markpad::DocumentToken& token,
                         const markpad::markdown::EditCommand& command, int) {
      QCOMPARE(token, session.snapshot().token);
      auto result = editor.apply(command);
      QVERIFY(result.document.has_value());
      QVERIFY(!session.replaceSource(token, result.document->source.toStdString()));
      ++commands;
    });
    auto cursor = editor.editor()->textCursor();
    cursor.setPosition(view);
    editor.editor()->setTextCursor(cursor);
    editor.editor()->insertPlainText(QStringLiteral("x"));
    QCOMPARE(commands, 1);
    QCOMPARE(session.snapshot().source, std::string("first\n\nsexcond\n"));
  }

  void unicodeSourceMappingRejectsSurrogateInterior() {
    NoFiles files;
    markpad::DocumentSession session(files);
    const std::string source = "A \xf0\x9f\x98\x80 Z\n";
    QVERIFY(!session.replaceSource(session.snapshot().token, source));
    markpad::ContinuousEditor editor;
    QVERIFY(editor.render(session.snapshot()).isEmpty());
    bool exact = false;
    const int before = editor.viewPosition(2, &exact);
    QVERIFY(exact);
    QCOMPARE(editor.sourcePosition(before).value(), qsizetype(2));
    QVERIFY(!editor.sourcePosition(before + 1).has_value());
    QCOMPARE(editor.sourcePosition(before + 2).value(), qsizetype(6));
    const int after = editor.viewPosition(6, &exact);
    QVERIFY(exact);
    QCOMPARE(after, before + 2);
    QCOMPARE(session.snapshot().source, source);
  }

  void productionComplexBlocksAndInertHtml() {
    NoFiles files;
    markpad::DocumentSession session(files);
    const std::string source =
        "intro\n\n| H | V |\n| --- | --- |\n| cell | value |\n\n"
        "```cpp\nint value = 1;\n```\n\n"
        "<script>untrusted_active_content()</script>\n";
    QVERIFY(!session.replaceSource(session.snapshot().token, source));
    markpad::ContinuousEditor editor;
    QVERIFY(editor.render(session.snapshot()).isEmpty());
    auto* document = editor.editor()->document();
    const auto cell = document->find(QStringLiteral("cell"));
    QVERIFY(!cell.isNull());
    QVERIFY(cell.currentTable() != nullptr);
    QVERIFY(!document->find(QStringLiteral("int value = 1;")).isNull());
    QVERIFY(!document->find(QStringLiteral("HTML - edit in Markdown")).isNull());
    QVERIFY(document->find(QStringLiteral("untrusted_active_content")).isNull());
    QCOMPARE(session.snapshot().source, source);
  }

  void richBlocksRemainIntactWhenMovedBetweenSheets() {
    QWidget firstSheet;
    QWidget secondSheet;
    QVBoxLayout firstLayout(&firstSheet);
    QVBoxLayout secondLayout(&secondSheet);
    QTextEdit richBlock;
    richBlock.setAcceptRichText(false);
    QTextCursor cursor(richBlock.document());
    auto* table = cursor.insertTable(2, 2);
    table->cellAt(0, 0).firstCursorPosition().insertText(QStringLiteral("header"));
    table->cellAt(1, 0).firstCursorPosition().insertText(QStringLiteral("cell"));
    cursor.movePosition(QTextCursor::End);
    cursor.insertBlock();
    cursor.insertText(QStringLiteral("code line one"));
    cursor.insertBlock();
    cursor.insertText(QStringLiteral("code line two"));
    const auto original = richBlock.toPlainText();
    auto cell = richBlock.document()->find(QStringLiteral("cell"));
    QVERIFY(cell.currentTable() == table);
    richBlock.setTextCursor(cell);
    firstLayout.addWidget(&richBlock);
    secondLayout.addWidget(&richBlock);
    QCOMPARE(richBlock.parentWidget(), &secondSheet);
    QCOMPARE(richBlock.toPlainText(), original);
    QVERIFY(richBlock.textCursor().currentTable() == table);
    QVERIFY(!richBlock.document()->find(QStringLiteral("code line one")).isNull());
    QVERIFY(!richBlock.document()->find(QStringLiteral("code line two")).isNull());
    richBlock.textCursor().insertText(QStringLiteral("new"));
    QVERIFY(!richBlock.document()->find(QStringLiteral("new")).isNull());
  }

  void productionTableEditAfterEditorReparenting() {
    QWidget firstSheet;
    QWidget secondSheet;
    QVBoxLayout firstLayout(&firstSheet);
    QVBoxLayout secondLayout(&secondSheet);
    NoFiles files;
    markpad::DocumentSession session(files);
    const std::string source = "| H |\n| --- |\n| cell |\n";
    QVERIFY(!session.replaceSource(session.snapshot().token, source));
    markpad::ContinuousEditor editor;
    QVERIFY(editor.render(session.snapshot()).isEmpty());
    firstLayout.addWidget(&editor);
    auto cursor = editor.editor()->document()->find(QStringLiteral("cell"));
    QVERIFY(!cursor.isNull());
    QVERIFY(cursor.currentTable());
    cursor.setPosition(cursor.selectionEnd());
    editor.editor()->setTextCursor(cursor);
    secondLayout.addWidget(&editor);
    QCOMPARE(editor.parentWidget(), &secondSheet);
    int commands = 0;
    QObject::connect(&editor, &markpad::ContinuousEditor::commandRequested,
                     [&](const markpad::DocumentToken& token,
                         const markpad::markdown::EditCommand& command, int) {
      QCOMPARE(token, session.snapshot().token);
      auto result = editor.apply(command);
      QVERIFY(result.document.has_value());
      QVERIFY(!session.replaceSource(token, result.document->source.toStdString()));
      ++commands;
    });
    editor.editor()->insertPlainText(QStringLiteral("x"));
    QCOMPARE(commands, 1);
    QCOMPARE(session.snapshot().source, std::string("| H |\n| --- |\n| cellx |\n"));
  }

  void productionCodeEditAfterEditorReparenting() {
    QWidget firstSheet;
    QWidget secondSheet;
    QVBoxLayout firstLayout(&firstSheet);
    QVBoxLayout secondLayout(&secondSheet);
    NoFiles files;
    markpad::DocumentSession session(files);
    const std::string source = "```cpp\nint x;\n```\n";
    QVERIFY(!session.replaceSource(session.snapshot().token, source));
    markpad::ContinuousEditor editor;
    QVERIFY(editor.render(session.snapshot()).isEmpty());
    firstLayout.addWidget(&editor);
    auto cursor = editor.editor()->document()->find(QStringLiteral("int x;"));
    QVERIFY(!cursor.isNull());
    cursor.setPosition(cursor.selectionEnd());
    editor.editor()->setTextCursor(cursor);
    secondLayout.addWidget(&editor);
    int commands = 0;
    QObject::connect(&editor, &markpad::ContinuousEditor::commandRequested,
                     [&](const markpad::DocumentToken& token,
                         const markpad::markdown::EditCommand& command, int) {
      QCOMPARE(token, session.snapshot().token);
      auto result = editor.apply(command);
      QVERIFY(result.document.has_value());
      QVERIFY(!session.replaceSource(token, result.document->source.toStdString()));
      ++commands;
    });
    editor.editor()->insertPlainText(QStringLiteral(" // note"));
    QCOMPARE(commands, 1);
    QCOMPARE(session.snapshot().source,
             std::string("```cpp\nint x; // note\n```\n"));
  }

  void sequentialPageEditsViaCanonicalProjection() {
    NoFiles files;
    markpad::DocumentSession session(files);
    const std::string source =
        "| H |\n| --- |\n| cell |\n\n```cpp\nint x;\n```\n";
    QVERIFY(!session.replaceSource(session.snapshot().token, source));
    markpad::ContinuousEditor canonical;
    QVERIFY(canonical.render(session.snapshot()).isEmpty());
    QTextEdit pages[2];
    pages[0].setPlainText(QStringLiteral("cell"));
    pages[1].setPlainText(QStringLiteral("int x;"));
    int commands = 0;
    QObject::connect(&canonical, &markpad::ContinuousEditor::commandRequested,
                     [&](const markpad::DocumentToken& token,
                         const markpad::markdown::EditCommand& command, int) {
      QCOMPARE(token, session.snapshot().token);
      auto result = canonical.apply(command);
      QVERIFY(result.document.has_value());
      QVERIFY(!session.replaceSource(token, result.document->source.toStdString()));
      ++commands;
    });

    // This is a deliberately narrow adapter: local insertion position is
    // translated from a known single-run starting position in the canonical
    // QTextDocument. It is not a general mapping for page fragments.
    auto cell = canonical.editor()->document()->find(QStringLiteral("cell"));
    QVERIFY(cell.currentTable());
    const int cellStart = cell.selectionStart();
    pages[0].moveCursor(QTextCursor::End);
    auto target = canonical.editor()->textCursor();
    target.setPosition(cellStart + pages[0].textCursor().position());
    canonical.editor()->setTextCursor(target);
    QObject::connect(&pages[0], &QTextEdit::textChanged, [&] {
      canonical.editor()->insertPlainText(pages[0].toPlainText().mid(4));
    });
    pages[0].insertPlainText(QStringLiteral("x"));
    QCOMPARE(commands, 1);
    QCOMPARE(session.snapshot().source,
             std::string("| H |\n| --- |\n| cellx |\n\n```cpp\nint x;\n```\n"));

    QVERIFY(canonical.render(session.snapshot()).isEmpty());
    auto code = canonical.editor()->document()->find(QStringLiteral("int x;"));
    QVERIFY(!code.isNull());
    const int codeStart = code.selectionStart();
    pages[1].moveCursor(QTextCursor::End);
    target = canonical.editor()->textCursor();
    target.setPosition(codeStart + pages[1].textCursor().position());
    canonical.editor()->setTextCursor(target);
    QObject::connect(&pages[1], &QTextEdit::textChanged, [&] {
      canonical.editor()->insertPlainText(pages[1].toPlainText().mid(6));
    });
    pages[1].insertPlainText(QStringLiteral(" // note"));
    QCOMPARE(commands, 2);
    QCOMPARE(session.snapshot().source,
             std::string("| H |\n| --- |\n| cellx |\n\n"
                         "```cpp\nint x; // note\n```\n"));
  }

  void proxyDeletionAndStaleRevision() {
    NoFiles files;
    markpad::DocumentSession session(files);
    QVERIFY(!session.replaceSource(session.snapshot().token,
        "| H |\n| --- |\n| cell |\n\n```cpp\nint x;\n```\n"));
    markpad::ContinuousEditor canonical;
    QVERIFY(canonical.render(session.snapshot()).isEmpty());
    QTextEdit pages[2];
    pages[0].setPlainText(QStringLiteral("cell"));
    pages[1].setPlainText(QStringLiteral("int x;"));
    QString previous[2] = {pages[0].toPlainText(), pages[1].toPlainText()};
    markpad::DocumentToken tokens[2] = {session.snapshot().token,
                                        session.snapshot().token};
    int starts[2] = {
        canonical.editor()->document()->find(previous[0]).selectionStart(),
        canonical.editor()->document()->find(previous[1]).selectionStart()};
    int applied = 0;
    int stale = 0;
    int rejected = 0;
    int savedCursor[2] = {0, 0};
    QObject::connect(&canonical, &markpad::ContinuousEditor::commandRequested,
                     [&](const markpad::DocumentToken& token,
                         const markpad::markdown::EditCommand& command, int) {
      QCOMPARE(token, session.snapshot().token);
      auto result = canonical.apply(command);
      QVERIFY(result.document.has_value());
      QVERIFY(!session.replaceSource(token, result.document->source.toStdString()));
      ++applied;
    });
    for (int i = 0; i < 2; ++i) {
      QObject::connect(&pages[i], &QTextEdit::textChanged, [&, i] {
        const auto current = pages[i].toPlainText();
        if (!(tokens[i] == session.snapshot().token)) {
          ++stale;
          const QSignalBlocker blocker(&pages[i]);
          pages[i].setPlainText(previous[i]);
          auto cursor = pages[i].textCursor();
          cursor.setPosition(std::min(savedCursor[i], int(previous[i].size())));
          pages[i].setTextCursor(cursor);
          return;
        }
        // Keep both edit boundaries on UTF-16 surrogate boundaries.
        const auto change = proxyChange(previous[i], current);
        const int prefix = change.prefix;
        const int suffix = change.suffix;
        auto target = canonical.editor()->textCursor();
        target.setPosition(starts[i] + prefix);
        target.setPosition(starts[i] + previous[i].size() - suffix,
                           QTextCursor::KeepAnchor);
        canonical.editor()->setTextCursor(target);
        canonical.editor()->insertPlainText(
            current.mid(prefix, current.size() - prefix - suffix));
        if (tokens[i] == session.snapshot().token) {
          ++rejected;
          const QSignalBlocker blocker(&pages[i]);
          pages[i].setPlainText(previous[i]);
          auto cursor = pages[i].textCursor();
          cursor.setPosition(std::min(savedCursor[i], int(previous[i].size())));
          pages[i].setTextCursor(cursor);
        } else {
          previous[i] = current;
          savedCursor[i] = pages[i].textCursor().position();
        }
      });
    }
    pages[0].moveCursor(QTextCursor::End);
    pages[0].textCursor().deletePreviousChar();
    QCOMPARE(applied, 1);
    QCOMPARE(session.snapshot().source,
             std::string("| H |\n| --- |\n| cel |\n\n```cpp\nint x;\n```\n"));
    pages[1].moveCursor(QTextCursor::End);
    savedCursor[1] = pages[1].textCursor().position();
    pages[1].insertPlainText(QStringLiteral(" stale"));
    QCOMPARE(stale, 1);
    QCOMPARE(pages[1].toPlainText(), QStringLiteral("int x;"));
    QCOMPARE(pages[1].textCursor().position(), 6);
    QCOMPARE(applied, 1);
    QVERIFY(canonical.render(session.snapshot()).isEmpty());
    tokens[1] = session.snapshot().token;
    starts[1] = canonical.editor()->document()->find(previous[1]).selectionStart();
    pages[1].moveCursor(QTextCursor::End);
    pages[1].insertPlainText(QStringLiteral(" // ok"));
    QCOMPARE(applied, 2);
    QCOMPARE(session.snapshot().source,
             std::string("| H |\n| --- |\n| cel |\n\n```cpp\nint x; // ok\n```\n"));
    QVERIFY(canonical.render(session.snapshot()).isEmpty());
    tokens[0] = session.snapshot().token;
    starts[0] = canonical.editor()->document()->find(previous[0]).selectionStart();
    pages[0].selectAll();
    pages[0].textCursor().removeSelectedText();
    QCOMPARE(rejected, 1);
    QCOMPARE(pages[0].toPlainText(), QStringLiteral("cel"));
    QCOMPARE(pages[0].textCursor().position(), savedCursor[0]);
    QCOMPARE(applied, 2);
    QCOMPARE(session.snapshot().source,
             std::string("| H |\n| --- |\n| cel |\n\n```cpp\nint x; // ok\n```\n"));
  }

  void canvasTypingOnSecondPageAndUndo() {
    QTextDocument document;
    markpad::spike::CanvasEdit canvas;
    canvas.resize(400, 400);
    canvas.setPageSize(QSizeF(200, 100));
    canvas.setDocument(&document);
    QByteArray text;
    for (int i = 0; i < 40; ++i) {
      text += "paragraph " + QByteArray::number(i) + "\n";
    }
    document.setPlainText(QString::fromUtf8(text));
    document.setModified(false);
    canvas.show();
    QApplication::processEvents();
    canvas.relayout();
    QVERIFY(canvas.pageCount() >= 3);
    QTextBlock firstSecondPage;
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
      if (document.documentLayout()->blockBoundingRect(block).top() >=
          canvas.documentTop(1)) {
        firstSecondPage = block;
        break;
      }
    }
    QVERIFY(firstSecondPage.isValid());
    QVERIFY(canvas.documentTop(1) > 0.0);
    const auto point = canvas.contentRect(1).topLeft().toPoint() + QPoint(2, 2);
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, point);
    QVERIFY2(canvas.textCursor().position() >= firstSecondPage.position(),
             qPrintable(QStringLiteral("cursor %1 expected >= %2, point %3,%4, pages %5")
                            .arg(canvas.textCursor().position())
                            .arg(firstSecondPage.position())
                            .arg(point.x())
                            .arg(point.y())
                            .arg(canvas.pageCount())));
    QTest::keyClicks(&canvas, QStringLiteral("X"));
    QVERIFY(document.toPlainText().contains(QStringLiteral("X")));
    QTest::keyClick(&canvas, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(document.toPlainText(), QString::fromUtf8(text));
  }

  void canvasSelectionAcrossPages() {
    QTextDocument document;
    markpad::spike::CanvasEdit canvas;
    canvas.resize(400, 400);
    canvas.setPageSize(QSizeF(200, 100));
    canvas.setDocument(&document);
    QByteArray text;
    for (int i = 0; i < 40; ++i) {
      text += "paragraph " + QByteArray::number(i) + "\n";
    }
    document.setPlainText(QString::fromUtf8(text));
    canvas.show();
    QApplication::processEvents();
    canvas.relayout();
    QVERIFY(canvas.pageCount() >= 3);
    const auto start = canvas.contentRect(0).topLeft().toPoint() + QPoint(2, 2);
    const auto end =
        canvas.contentRect(2).bottomLeft().toPoint() + QPoint(2, -2);
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(canvas.viewport(), end);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, end);
    const auto selection = canvas.textCursor().selectedText();
    QVERIFY(!selection.isEmpty());
    QVERIFY(selection.contains(QStringLiteral("paragraph 0")));
    QVERIFY(selection.contains(QStringLiteral("paragraph 12")));
  }

  void canvasImeCommitOnSecondPage() {
    QTextDocument document;
    markpad::spike::CanvasEdit canvas;
    canvas.resize(400, 400);
    canvas.setPageSize(QSizeF(200, 100));
    canvas.setDocument(&document);
    QByteArray text;
    for (int i = 0; i < 40; ++i) {
      text += "paragraph " + QByteArray::number(i) + "\n";
    }
    document.setPlainText(QString::fromUtf8(text));
    canvas.show();
    QApplication::processEvents();
    canvas.relayout();
    const auto point = canvas.contentRect(1).topLeft().toPoint() + QPoint(2, 2);
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, point);
    QInputMethodEvent event;
    event.setCommitString(QString::fromUtf8("\xc3\xa9"));
    QApplication::sendEvent(&canvas, &event);
    QVERIFY(document.toPlainText().contains(QString::fromUtf8("\xc3\xa9")));
  }

  void canvasRendersTextOnEachSheet() {
    QTextDocument document;
    markpad::spike::CanvasEdit canvas;
    canvas.resize(400, 400);
    canvas.setPageSize(QSizeF(200, 100));
    canvas.setDocument(&document);
    QByteArray text;
    for (int i = 0; i < 40; ++i) {
      text += "paragraph " + QByteArray::number(i) + "\n";
    }
    document.setPlainText(QString::fromUtf8(text));
    canvas.show();
    QApplication::processEvents();
    canvas.relayout();
    QVERIFY(canvas.pageCount() >= 3);
    const auto shot = canvas.viewport()->grab().toImage();
    QVERIFY(!shot.isNull());
    const auto content = canvas.contentRect(1);
    bool dark = false;
    for (int y = int(content.top()); y < int(content.bottom()) && !dark; ++y) {
      for (int x = int(content.left()); x < int(content.right()); ++x) {
        if (shot.pixelColor(x, y).lightness() < 128) {
          dark = true;
          break;
        }
      }
    }
    QVERIFY(dark);
    QTextBlock previous;
    QTextBlock first;
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
      const auto top =
          document.documentLayout()->blockBoundingRect(block).top();
      if (top < canvas.documentTop(1)) {
        previous = block;
      } else if (top >= canvas.documentTop(1)) {
        first = block;
        break;
      }
    }
    QVERIFY(previous.isValid());
    QVERIFY(first.isValid());
    const auto previousRect =
        document.documentLayout()->blockBoundingRect(previous);
    QVERIFY(previousRect.bottom() <= canvas.documentTop(1) + 0.5);
  }

  void qtNativePaginationSplitsOversizedParagraph() {
    QTextDocument document;
    document.setPageSize(QSizeF(200, 100));
    document.setPlainText(QStringLiteral("long text ").repeated(400));
    const auto block = document.begin();
    QVERIFY(block.isValid());
    QVERIFY(!block.next().isValid());
    QVERIFY(document.pageCount() > 1);
    QVERIFY(document.documentLayout()->blockBoundingRect(block).height() > 100);
  }

  void qtNativePaginationSplitsOversizedTable() {
    QTextDocument document;
    document.setPageSize(QSizeF(200, 100));
    QTextCursor cursor(&document);
    auto* table = cursor.insertTable(30, 2);
    for (int row = 0; row < 30; ++row) {
      auto cell = table->cellAt(row, 0).firstCursorPosition();
      cell.insertText(QStringLiteral("row %1").arg(row));
    }
    QVERIFY(document.pageCount() > 1);
    const auto first = table->cellAt(0, 0).firstCursorPosition().block();
    const auto last = table->cellAt(29, 0).firstCursorPosition().block();
    const auto firstRect = document.documentLayout()->blockBoundingRect(first);
    const auto lastRect = document.documentLayout()->blockBoundingRect(last);
    QVERIFY(firstRect.top() < 100);
    QVERIFY(lastRect.top() >= 100);
  }
};
}  // namespace

QTEST_MAIN(PagesTest)
#include "pages_test.moc"
