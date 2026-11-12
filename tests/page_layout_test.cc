#include <QFont>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <QtTest/QtTest>
#include <memory>
#include <set>

#include "page_layout.h"

namespace markpad {
namespace {

PageSetup a4() {
  PageSetup setup;
  setup.size = PageSize::kA4;
  setup.orientation = PageOrientation::kPortrait;
  return setup;
}

std::unique_ptr<QTextDocument> documentFor(const QByteArray& html,
                                           const PageSetup& setup) {
  auto document = std::make_unique<QTextDocument>();
  document->setDefaultFont(QFont(QStringLiteral("DejaVu Sans"), 10));
  const auto page = PageLayoutEngine::pageSize(setup);
  const auto width =
      page.width() - (setup.marginLeftMm + setup.marginRightMm) * 72.0 / 25.4;
  document->setTextWidth(width);
  document->setHtml(QString::fromUtf8(html));
  return document;
}

}  // namespace

class PageLayoutTest : public QObject {
  Q_OBJECT

 private slots:
  void geometry() {
    const auto page = PageLayoutEngine::pageSize(a4());
    QVERIFY(page.width() > 500.0);
    QVERIFY(page.height() > 800.0);
    auto landscape = a4();
    landscape.orientation = PageOrientation::kLandscape;
    const auto rotated = PageLayoutEngine::pageSize(landscape);
    QCOMPARE(rotated.width(), page.height());
    QCOMPARE(rotated.height(), page.width());
    const auto result = PageLayoutEngine::layout(*documentFor("<p>one</p>", a4()), a4());
    QCOMPARE(result.pages.size(), size_t(1));
    QCOMPARE(result.placements.size(), size_t(1));
    QVERIFY(!result.overflow);
  }

  void viewScaleGeometry() {
    const auto points = PageLayoutEngine::pageSize(a4());
    const auto pixels = PageLayoutEngine::pageSize(a4(), 96.0 / 72.0);
    QVERIFY(pixels.width() > points.width() * 1.3);
    QVERIFY(pixels.height() > points.height() * 1.3);
    const auto layout = PageLayoutEngine::layout(
        *documentFor("<p>one</p>", a4()), a4(), 96.0 / 72.0);
    QCOMPARE(layout.pages.size(), size_t(1));
    QVERIFY(layout.pages.front().contentRect.width() >
            PageLayoutEngine::layout(*documentFor("<p>one</p>", a4()), a4())
                .pages.front()
                .contentRect.width());
  }

  void paragraphFlowAndMargins() {
    QByteArray html;
    for (int i = 0; i < 180; ++i) {
      html += "<p>paragraph " + QByteArray::number(i) + "</p>";
    }
    const auto normal = PageLayoutEngine::layout(*documentFor(html, a4()), a4());
    QVERIFY(normal.pages.size() > 2);

    auto landscape = a4();
    landscape.orientation = PageOrientation::kLandscape;
    const auto wide =
        PageLayoutEngine::layout(*documentFor(html, landscape), landscape);
    QVERIFY(wide.pages.size() >= normal.pages.size());

    auto wideMargins = a4();
    wideMargins.marginTopMm = 50.0;
    wideMargins.marginBottomMm = 50.0;
    wideMargins.marginLeftMm = 50.0;
    wideMargins.marginRightMm = 50.0;
    const auto narrow = PageLayoutEngine::layout(*documentFor(html, wideMargins), wideMargins);
    QVERIFY(narrow.pages.size() >= normal.pages.size());
  }

  void oversizedBlockOverflows() {
    const auto result = PageLayoutEngine::layout(*documentFor("<p style=\"font-size:300pt\">huge</p>", a4()), a4());
    QVERIFY(result.overflow);
    QVERIFY(!result.warning.isEmpty());
    QCOMPARE(result.placements.size(), size_t(1));
    QVERIFY(result.placements.front().overflow);
    QCOMPARE(result.placements.front().page, 0);
  }

  void pageForPositionAndDeterminism() {
    QByteArray html;
    for (int i = 0; i < 100; ++i) {
      html += "<p>paragraph " + QByteArray::number(i) + "</p>";
    }
    const auto first = PageLayoutEngine::layout(*documentFor(html, a4()), a4());
    const auto second = PageLayoutEngine::layout(*documentFor(html, a4()), a4());
    QCOMPARE(first.pages.size(), second.pages.size());
    QCOMPARE(first.placements.size(), second.placements.size());
    for (size_t i = 0; i < first.placements.size(); ++i) {
      QCOMPARE(first.placements[i].block, second.placements[i].block);
      QCOMPARE(first.placements[i].page, second.placements[i].page);
    }
    auto document = documentFor(html, a4());
    const auto page = PageLayoutEngine::pageForPosition(*document, a4(), 0);
    QCOMPARE(page, 0);
    const auto last = PageLayoutEngine::pageForPosition(
        *document, a4(), document->characterCount() - 1);
    QVERIFY(last >= 0);
    QVERIFY(last < int(first.pages.size()));
  }

  void richBlocksFlow() {
    QByteArray html =
        "<h1>Heading</h1><p>Paragraph text</p>"
        "<ul><li>one</li><li>two</li></ul>"
        "<pre>code line\nsecond line</pre>"
        "<table border=\"1\"><tr><th>h</th></tr><tr><td>cell</td></tr></table>"
        "<p><img src=\"image.png\" alt=\"alt\"></p>";
    for (int i = 0; i < 12; ++i) {
      html += "<h2>Section " + QByteArray::number(i) + "</h2><p>body</p>";
    }
    const auto result = PageLayoutEngine::layout(*documentFor(html, a4()), a4());
    QVERIFY(!result.placements.empty());
    QVERIFY(result.pages.size() >= 1);
    for (size_t i = 0; i < result.placements.size(); ++i) {
      QVERIFY(result.placements[i].page >= 0);
      if (i > 0) {
        QVERIFY(result.placements[i].page >=
                result.placements[i - 1].page);
      }
    }
  }

  void blocksDoNotCrossPageBounds() {
    QByteArray html;
    for (int i = 0; i < 20; ++i) {
      html += "<p style=\"font-size:80pt\">block " + QByteArray::number(i) +
              "</p>";
    }
    const auto result = PageLayoutEngine::layout(*documentFor(html, a4()), a4());
    QVERIFY(result.pages.size() > 1);
    const auto contentHeight = result.pages.front().contentRect.height();
    for (size_t page = 0; page < result.pages.size(); ++page) {
      double used = 0.0;
      bool overflow = false;
      for (const auto& placement : result.placements) {
        if (placement.page != int(page)) {
          continue;
        }
        used += placement.rect.height();
        overflow = overflow || placement.overflow;
      }
      if (!overflow) {
        QVERIFY(used <= contentHeight + 0.5);
      }
    }
  }

  void tablesStayOnOnePage() {
    QByteArray html;
    for (int i = 0; i < 18; ++i) {
      html += "<p>paragraph " + QByteArray::number(i) + "</p>";
    }
    html += "<table border=\"1\"><tr><th>h1</th><th>h2</th></tr>";
    for (int i = 0; i < 8; ++i) {
      html += "<tr><td>cell " + QByteArray::number(i) +
              "</td><td>other</td></tr>";
    }
    html += "</table><p>after</p>";
    auto document = documentFor(html, a4());
    std::set<int> tableBlocks;
    for (auto block = document->begin(); block.isValid(); block = block.next()) {
      QTextCursor cursor(block);
      if (cursor.currentTable()) {
        tableBlocks.insert(block.blockNumber());
      }
    }
    QVERIFY(!tableBlocks.empty());
    const auto result = PageLayoutEngine::layout(*document, a4());
    int tablePage = -1;
    int covering = 0;
    for (const auto& placement : result.placements) {
      bool intersects = false;
      for (const auto block : tableBlocks) {
        if (block >= placement.block && block <= placement.endBlock) {
          intersects = true;
          break;
        }
      }
      if (!intersects) {
        continue;
      }
      ++covering;
      if (tablePage < 0) {
        tablePage = placement.page;
      }
      QCOMPARE(placement.page, tablePage);
      QVERIFY(placement.block <= *tableBlocks.begin());
      QVERIFY(placement.endBlock >= *tableBlocks.rbegin());
    }
    QCOMPARE(covering, 1);
  }

  void atomicCodeUnitsStayOnOnePage() {
    QByteArray html;
    for (int i = 0; i < 12; ++i) {
      html += "<p>paragraph " + QByteArray::number(i) + "</p>";
    }
    html += "<p>code line 1</p><p>code line 2</p><p>code line 3</p>";
    for (int i = 0; i < 12; ++i) {
      html += "<p>tail " + QByteArray::number(i) + "</p>";
    }
    auto document = documentFor(html, a4());
    int marked = 0;
    for (auto block = document->begin(); block.isValid(); block = block.next()) {
      if (block.text().startsWith(QStringLiteral("code line"))) {
        auto format = block.blockFormat();
        format.setProperty(kPageUnitProperty, 1234);
        format.setTopMargin(0);
        format.setBottomMargin(0);
        QTextCursor cursor(block);
        cursor.setBlockFormat(format);
        ++marked;
      }
    }
    QCOMPARE(marked, 3);
    const auto result = PageLayoutEngine::layout(*document, a4());
    int codePage = -1;
    int covering = 0;
    for (const auto& placement : result.placements) {
      if (placement.block <= 12 && placement.endBlock >= 14) {
        ++covering;
        codePage = placement.page;
      } else if (placement.block >= 12 && placement.block <= 14) {
        QCOMPARE(placement.page, codePage);
      }
    }
    QCOMPARE(covering, 1);
  }

  void invalidMarginsReportError() {
    auto setup = a4();
    setup.marginTopMm = 200.0;
    setup.marginBottomMm = 200.0;
    const auto result =
        PageLayoutEngine::layout(*documentFor("<p>x</p>", setup), setup);
    QVERIFY(result.overflow);
    QVERIFY(!result.warning.isEmpty());
    QCOMPARE(result.pages.size(), size_t(1));
  }
};

}  // namespace markpad

QTEST_MAIN(markpad::PageLayoutTest)

#include "page_layout_test.moc"
