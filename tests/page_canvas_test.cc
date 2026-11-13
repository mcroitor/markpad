#include <QFont>
#include <QInputMethodEvent>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QtTest/QtTest>
#include <memory>

#include "page_canvas.h"

namespace markpad {
namespace {

std::unique_ptr<QTextDocument> documentFor(int paragraphs) {
  auto document = std::make_unique<QTextDocument>();
  document->setDefaultFont(QFont(QStringLiteral("DejaVu Sans"), 10));
  QByteArray text;
  for (int i = 0; i < paragraphs; ++i) {
    text += "paragraph " + QByteArray::number(i) + "\n";
  }
  document->setPlainText(QString::fromUtf8(text));
  return document;
}

int firstBlockOnPage(const PageCanvas& canvas, int page) {
  for (const auto& unit : canvas.pagePlan().units) {
    if (unit.page == page) {
      return unit.firstBlock;
    }
  }
  return -1;
}

}  // namespace

class PageCanvasTest : public QObject {
  Q_OBJECT

 private slots:
  void paginatesAndTypesOnSecondPage() {
    auto document = documentFor(200);
    PageCanvas canvas;
    canvas.setViewScale(1.0);
    canvas.setDocument(document.get());
    canvas.resize(1000, 800);
    canvas.show();
    QApplication::processEvents();
    canvas.refreshLayout();
    QVERIFY(canvas.pageCount() >= 3);
    const int firstSecond = firstBlockOnPage(canvas, 1);
    QVERIFY(firstSecond >= 0);
    const auto point = canvas.contentRect(1).topLeft().toPoint() + QPoint(4, 4);
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, point);
    QVERIFY(canvas.textCursor().blockNumber() >= firstSecond);
    const auto before = document->toPlainText();
    QTest::keyClicks(&canvas, QStringLiteral("X"));
    QVERIFY(document->toPlainText() != before);
    QVERIFY(document->toPlainText().contains(QStringLiteral("X")));
    QTest::keyClick(&canvas, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(document->toPlainText(), before);
  }

  void selectionCrossesPages() {
    auto document = documentFor(200);
    PageCanvas canvas;
    canvas.setViewScale(1.0);
    canvas.setDocument(document.get());
    canvas.resize(1000, 800);
    canvas.show();
    QApplication::processEvents();
    canvas.refreshLayout();
    QVERIFY(canvas.pageCount() >= 3);
    const auto start = canvas.contentRect(0).topLeft().toPoint() + QPoint(1, 2);
    const auto end =
        canvas.contentRect(2).bottomLeft().toPoint() + QPoint(4, -4);
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(canvas.viewport(), end);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, end);
    QVERIFY(canvas.textCursor().hasSelection());
    const auto selected = canvas.textCursor().selectedText();
    QVERIFY2(selected.contains(QStringLiteral("paragraph 0")),
             qPrintable(QStringLiteral("start %1 end %2 text '%3'")
                            .arg(canvas.textCursor().selectionStart())
                            .arg(canvas.textCursor().selectionEnd())
                            .arg(selected.left(80))));
    const int firstThird = firstBlockOnPage(canvas, 2);
    QVERIFY(firstThird >= 0);
    const auto firstText =
        document->findBlockByNumber(firstThird).text().left(9);
    QVERIFY(selected.contains(firstText));
  }

  void imePreeditAndCommit() {
    auto document = documentFor(200);
    PageCanvas canvas;
    canvas.setViewScale(1.0);
    canvas.setDocument(document.get());
    canvas.resize(1000, 800);
    canvas.show();
    QApplication::processEvents();
    canvas.refreshLayout();
    const auto point = canvas.contentRect(1).topLeft().toPoint() + QPoint(4, 4);
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, point);
    const auto before = document->toPlainText();
    QInputMethodEvent preedit(QStringLiteral("compose"), {});
    QApplication::sendEvent(&canvas, &preedit);
    QCOMPARE(document->toPlainText(), before);
    QInputMethodEvent commit;
    commit.setCommitString(QString::fromUtf8("\xc3\xa9"));
    QApplication::sendEvent(&canvas, &commit);
    QVERIFY(document->toPlainText().contains(QString::fromUtf8("\xc3\xa9")));
  }

  void qtextEditNotifiesExternalDocumentEdits() {
    QTextEdit edit;
    int notifications = 0;
    connect(&edit, &QTextEdit::textChanged, [&] { ++notifications; });
    QTextCursor cursor(edit.document());
    cursor.insertText(QStringLiteral("external"));
    QCOMPARE(notifications, 1);
    QCOMPARE(edit.toPlainText(), QStringLiteral("external"));
  }

  void oversizedBlockExtendsSheet() {
    QTextDocument document;
    document.setHtml(QStringLiteral("<p style=\"font-size:300pt\">huge</p>"));
    PageCanvas canvas;
    canvas.setViewScale(1.0);
    canvas.setDocument(&document);
    canvas.resize(1000, 800);
    canvas.show();
    QApplication::processEvents();
    canvas.refreshLayout();
    QVERIFY(canvas.hasOverflow());
    QVERIFY(!canvas.layoutWarning().isEmpty());
    const auto normal = PageLayoutEngine::pageSize(canvas.pageSetup(), 1.0);
    QVERIFY(canvas.pageRect(0).height() > normal.height());
  }

  void marginsChangePageCountAndKeepCursor() {
    auto document = documentFor(200);
    PageCanvas canvas;
    canvas.setViewScale(1.0);
    canvas.setDocument(document.get());
    canvas.resize(1000, 800);
    canvas.show();
    QApplication::processEvents();
    canvas.refreshLayout();
    const int before = canvas.pageCount();
    auto cursor = canvas.textCursor();
    cursor.setPosition(document->findBlockByNumber(100).position());
    canvas.setTextCursor(cursor);
    PageSetup setup = canvas.pageSetup();
    setup.marginTopMm = 50.0;
    setup.marginBottomMm = 50.0;
    setup.marginLeftMm = 50.0;
    setup.marginRightMm = 50.0;
    canvas.setPageSetup(setup);
    QVERIFY(canvas.pageCount() >= before);
    QVERIFY(canvas.currentPage() >= 0);
    QVERIFY(canvas.currentPage() < canvas.pageCount());
    QCOMPARE(canvas.textCursor().blockNumber(), 100);
  }
};

}  // namespace markpad

QTEST_MAIN(markpad::PageCanvasTest)
#include "page_canvas_test.moc"
