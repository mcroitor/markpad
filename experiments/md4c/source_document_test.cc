#include "source_document.h"

#include <QtTest/QtTest>
#include <algorithm>
#include <limits>

namespace markpad::md4c_spike {
namespace {

struct TextEditResult {
  std::optional<QByteArray> source;
  QString error;
};

// Legacy fixture assertions compare bytes; commands still use the public API.
TextEditResult editBytes(const Document& document, const EditCommand& command) {
  auto result = applyEdit(document, snapshotToken(document), command);
  if (!result.document) {
    return {std::nullopt, result.error};
  }
  return {result.document->source, {}};
}

TextEditResult replaceText(const Document& document, size_t node,
                           const QByteArray& text) {
  return editBytes(document, ReplaceText{node, text});
}

TextEditResult setTaskChecked(const Document& document, size_t node,
                              bool checked) {
  return editBytes(document, SetTaskChecked{node, checked});
}

ParseResult setHeadingLevel(const Document& document,
                            const SnapshotToken& token, size_t node,
                            int level) {
  return applyEdit(document, token, SetHeadingLevel{node, level});
}

size_t find(const Document& document, NodeKind kind, int type,
            const QByteArray& text = {}) {
  for (size_t i = 0; i < document.nodes.size(); ++i) {
    const auto& node = document.nodes[i];
    if (node.kind == kind && node.type == type &&
        (text.isEmpty() || node.text == text)) {
      return i;
    }
  }
  return document.nodes.size();
}

}  // namespace

class Md4cSourceTest : public QObject {
  Q_OBJECT

 private slots:
  void textEdit_data() {
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<int>("block");
    QTest::newRow("heading")
        << QByteArray("before\n======\n") << int(MD_BLOCK_H);
    QTest::newRow("emphasis")
        << QByteArray("_before_ and __bold__\n") << int(MD_BLOCK_P);
    QTest::newRow("strikethrough")
        << QByteArray("~~before~~\n") << int(MD_BLOCK_P);
    QTest::newRow("aligned-table")
        << QByteArray(
               "| L | M | R |\n| :--- | :---: | ---: |\n| before | 2 | 3 |\n")
        << int(MD_BLOCK_TABLE);
    QTest::newRow("table-code-pipe")
        << QByteArray("| Code | Text |\n| --- | --- |\n| `a\\|b` | before |\n")
        << int(MD_BLOCK_TABLE);
    QTest::newRow("tasks") << QByteArray("* [ ] before\n* [X] done\n")
                           << int(MD_BLOCK_LI);
    QTest::newRow("quoted-task")
        << QByteArray("> - [ ] before\n") << int(MD_BLOCK_QUOTE);
    QTest::newRow("ordered-tasks")
        << QByteArray("3. [ ] before\n4. [x] done\n") << int(MD_BLOCK_OL);
    QTest::newRow("nested-task")
        << QByteArray("- parent\n  - [ ] before\n") << int(MD_BLOCK_LI);
    QTest::newRow("link")
        << QByteArray(
               "[before][ref]\n\n[ref]: https://example.test \"title\"\n")
        << int(MD_BLOCK_P);
    QTest::newRow("unknown")
        << QByteArray(":::note\nbefore\n:::\n") << int(MD_BLOCK_P);
    QTest::newRow("malformed")
        << QByteArray("*unclosed\n\nbefore\n") << int(MD_BLOCK_P);
    QTest::newRow("crlf") << QByteArray("before\r\n") << int(MD_BLOCK_P);
    QTest::newRow("unicode-prefix")
        << QStringLiteral("\u041f\u0440\u0438\u0432\u0435\u0442\n\nbefore\n")
               .toUtf8()
        << int(MD_BLOCK_P);
    QTest::newRow("inline-html")
        << QByteArray("<span data-keep=\"raw\">before</span>\n")
        << int(MD_BLOCK_P);
  }

  void textEdit() {
    QFETCH(QByteArray, body);
    QFETCH(int, block);
    const QByteArray prefix =
        "<div data-keep=\"raw\">unchanged</div>\n\n~~~cpp\nkeep();\n~~~\n\n";
    const QByteArray suffix = "\n<!-- untouched -->\n\nlast  \nline\n";
    const auto source = prefix + body + suffix;
    auto parsed = parse(source);
    QVERIFY2(parsed.document, qPrintable(parsed.error));
    const auto& document = *parsed.document;
    QCOMPARE(document.source, source);
    QVERIFY(find(document, NodeKind::kBlock, block) < document.nodes.size());
    auto index = find(document, NodeKind::kText, MD_TEXT_NORMAL, "before");
    QVERIFY(index < document.nodes.size());
    const auto& node = document.nodes[index];
    QVERIFY(node.source);
    const auto range = *node.source;
    QCOMPARE(source.mid(range.offset, range.size), QByteArray("before"));
    auto result = replaceText(document, index, "after");
    QVERIFY2(result.source, qPrintable(result.error));
    QCOMPARE(result.source->left(range.offset), source.left(range.offset));
    QCOMPARE(result.source->mid(range.offset + 5),
             source.mid(range.offset + range.size));
    auto expected = source;
    expected.replace(range.offset, range.size, "after");
    QCOMPARE(*result.source, expected);
    QCOMPARE(document.source, source);
    qInfo() << QTest::currentDataTag()
            << "text edit preserves all bytes outside mapped leaf";
  }

  void gfmDetails() {
    auto parsed = parse(
        "# heading\n\n_italic_ **bold** ~~deleted~~\n\n"
        "| L | M | R |\n| :--- | :---: | ---: |\n| a | b | c |\n\n"
        "> - [ ] quoted\n\n3. [X] ordered\n4. [ ] next\n");
    QVERIFY2(parsed.document, qPrintable(parsed.error));
    const auto& doc = *parsed.document;
    const auto heading = find(doc, NodeKind::kBlock, MD_BLOCK_H);
    QVERIFY(heading < doc.nodes.size());
    QCOMPARE(doc.nodes[heading].attributes["level"].toInt(), 1);
    for (auto span : {MD_SPAN_EM, MD_SPAN_STRONG, MD_SPAN_DEL}) {
      QVERIFY(find(doc, NodeKind::kSpan, span) < doc.nodes.size());
    }
    const auto list = find(doc, NodeKind::kBlock, MD_BLOCK_OL);
    QVERIFY(list < doc.nodes.size());
    QCOMPARE(doc.nodes[list].attributes["start"].toInt(), 3);
    std::vector<int> alignments;
    int taskCount = 0;
    int checked = 0;
    for (const auto& node : doc.nodes) {
      if (node.kind == NodeKind::kBlock && node.type == MD_BLOCK_TH) {
        alignments.push_back(node.attributes["align"].toInt());
      }
      if (node.attributes["task"].toBool()) {
        ++taskCount;
        checked += node.attributes["checked"].toBool();
        QVERIFY(node.source);
        QVERIFY(node.source->offset < doc.source.size());
      }
    }
    QCOMPARE(alignments, (std::vector<int>{MD_ALIGN_LEFT, MD_ALIGN_CENTER,
                                           MD_ALIGN_RIGHT}));
    QCOMPARE(taskCount, 3);
    QCOMPARE(checked, 1);
  }

  void toggleTask_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::newRow("quoted") << QByteArray("> * [ ] same\r\n");
    QTest::newRow("ordered") << QByteArray("3. [ ] same\n4. [X] same\n");
    QTest::newRow("nested") << QByteArray("- parent\n  - [ ] same\n");
  }

  void toggleTask() {
    QFETCH(QByteArray, source);
    auto parsed = parse(source);
    QVERIFY2(parsed.document, qPrintable(parsed.error));
    const auto& doc = *parsed.document;
    const auto index = find(doc, NodeKind::kBlock, MD_BLOCK_LI);
    auto task = index;
    while (task < doc.nodes.size() &&
           !doc.nodes[task].attributes["task"].toBool()) {
      ++task;
    }
    QVERIFY(task < doc.nodes.size());
    const auto range = *doc.nodes[task].source;
    auto result = setTaskChecked(doc, task, true);
    QVERIFY2(result.source, qPrintable(result.error));
    auto expected = source;
    expected[range.offset] = 'x';
    QCOMPARE(*result.source, expected);
    auto reparsed = parse(*result.source);
    QVERIFY(reparsed.document);
    auto reversed = setTaskChecked(*reparsed.document, task, false);
    QVERIFY2(reversed.source, qPrintable(reversed.error));
    QCOMPARE(*reversed.source, source);
    auto noChange = setTaskChecked(doc, task, false);
    QVERIFY(noChange.source);
    QCOMPARE(*noChange.source, source);
  }

  void duplicateTextUsesOffsets() {
    auto parsed = parse("same\n\n_same_\n\n> same\n");
    QVERIFY(parsed.document);
    std::vector<size_t> leaves;
    for (size_t i = 0; i < parsed.document->nodes.size(); ++i) {
      if (parsed.document->nodes[i].text == "same") {
        leaves.push_back(i);
      }
    }
    QCOMPARE(leaves.size(), size_t{3});
    auto result = replaceText(*parsed.document, leaves[1], "other");
    QVERIFY2(result.source, qPrintable(result.error));
    QCOMPARE(*result.source, QByteArray("same\n\n_other_\n\n> same\n"));
  }

  void escapesAndUnicode_data() {
    QTest::addColumn<QByteArray>("text");
    QTest::newRow("markup") << QByteArray("*bold* | [link](target) & <tag>");
    QTest::newRow("unicode")
        << QStringLiteral("\u041f\u0440\u0438\u0432\u0435\u0442 \U0001f600")
               .toUtf8();
    QTest::newRow("autolink-looking") << QByteArray("https://example.test");
  }

  void escapesAndUnicode() {
    QFETCH(QByteArray, text);
    auto parsed = parse("**before**\n");
    QVERIFY(parsed.document);
    const auto index =
        find(*parsed.document, NodeKind::kText, MD_TEXT_NORMAL, "before");
    auto result = replaceText(*parsed.document, index, text);
    QVERIFY2(result.source, qPrintable(result.error));
    QVERIFY(result.source->startsWith("**"));
    QVERIFY(result.source->endsWith("**\n"));
  }

  void opaqueAndMalformedContent() {
    const QByteArray source =
        "<div data-x=\"y\">raw</div>\n\nbefore <span "
        "data-z=\"k\">text</span>\n\n"
        "~~~cpp\ncode\n~~~\n\n:::unknown\nkept\n:::\n\n*unclosed\n";
    auto parsed = parse(source);
    QVERIFY2(parsed.document, qPrintable(parsed.error));
    QCOMPARE(parsed.document->source, source);
    QVERIFY(find(*parsed.document, NodeKind::kText, MD_TEXT_HTML) <
            parsed.document->nodes.size());
    QVERIFY(find(*parsed.document, NodeKind::kText, MD_TEXT_CODE) <
            parsed.document->nodes.size());
    for (size_t i = 0; i < parsed.document->nodes.size(); ++i) {
      const auto& node = parsed.document->nodes[i];
      if (node.kind == NodeKind::kText &&
          (node.type == MD_TEXT_HTML || node.type == MD_TEXT_CODE)) {
        auto result = replaceText(*parsed.document, i, "changed");
        QVERIFY(!result.source);
        QVERIFY(!result.error.isEmpty());
      }
    }
  }

  void copiedCallbackDataAndNoChange() {
    auto parsed = parse(
        QByteArray("**same** &amp; [link](target \"title\")\n\n- [X] task\n"));
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto link = find(doc, NodeKind::kSpan, MD_SPAN_A);
    QVERIFY(link < doc.nodes.size());
    QCOMPARE(doc.nodes[link].attributes["href"].toString(),
             QStringLiteral("target"));
    QCOMPARE(doc.nodes[link].attributes["title"].toString(),
             QStringLiteral("title"));
    auto index = find(doc, NodeKind::kText, MD_TEXT_NORMAL, "same");
    QVERIFY(index < doc.nodes.size());
    auto unchangedText = replaceText(doc, index, "same");
    QVERIFY(unchangedText.source);
    QCOMPARE(*unchangedText.source, doc.source);
    auto task = find(doc, NodeKind::kBlock, MD_BLOCK_LI);
    QVERIFY(task < doc.nodes.size());
    auto unchangedTask = setTaskChecked(doc, task, true);
    QVERIFY(unchangedTask.source);
    QCOMPARE(*unchangedTask.source, doc.source);
    auto entity = find(doc, NodeKind::kText, MD_TEXT_ENTITY);
    QVERIFY(entity < doc.nodes.size());
    auto rejected = replaceText(doc, entity, "new");
    QVERIFY(!rejected.source);
    QVERIFY(!rejected.error.isEmpty());
  }

  void linksAndUnclosedCode() {
    auto parsed = parse(
        "www.example.com and user@example.com ![alt](missing.png \"title\")\n\n"
        "```cpp\nunfinished\n");
    QVERIFY2(parsed.document, qPrintable(parsed.error));
    const auto& doc = *parsed.document;
    std::vector<QString> links;
    for (const auto& node : doc.nodes) {
      if (node.kind == NodeKind::kSpan && node.type == MD_SPAN_A) {
        links.push_back(node.attributes["href"].toString());
      }
    }
    QCOMPARE(links,
             (std::vector<QString>{QStringLiteral("http://www.example.com"),
                                   QStringLiteral("mailto:user@example.com")}));
    const auto image = find(doc, NodeKind::kSpan, MD_SPAN_IMG);
    QVERIFY(image < doc.nodes.size());
    QCOMPARE(doc.nodes[image].attributes["src"].toString(),
             QStringLiteral("missing.png"));
    QCOMPARE(doc.nodes[image].attributes["title"].toString(),
             QStringLiteral("title"));
    const auto code = find(doc, NodeKind::kBlock, MD_BLOCK_CODE);
    QVERIFY(code < doc.nodes.size());
    QCOMPARE(doc.nodes[code].attributes["lang"].toString(),
             QStringLiteral("cpp"));
    QVERIFY(find(doc, NodeKind::kText, MD_TEXT_CODE) < doc.nodes.size());
    QVERIFY(doc.source.endsWith("```cpp\nunfinished\n"));
    qInfo("Unclosed fence preserved as source, not normalized or auto-closed");
  }

  void rejectsUnsupportedEdits() {
    auto parsed = parse("_before_\n");
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto index = find(doc, NodeKind::kText, MD_TEXT_NORMAL, "before");
    for (const auto& value :
         {QByteArray{}, QByteArray("line\nbreak"), QByteArray("line\rbreak"),
          QByteArray(1, '\0'), QByteArray(1, char(0xff))}) {
      const auto result = replaceText(doc, index, value);
      QVERIFY(!result.source);
      QVERIFY(!result.error.isEmpty());
    }
    // Whitespace at an emphasis boundary would destroy the surrounding span.
    const auto dangerous = replaceText(doc, index, " space ");
    QVERIFY(!dangerous.source);
    QVERIFY(dangerous.error.contains("semantics"));
    QVERIFY(!replaceText(doc, std::numeric_limits<size_t>::max(), "x").source);
    QVERIFY(!replaceText(doc, 0, "x").source);
    QVERIFY(!setTaskChecked(doc, index, true).source);
    auto invalid = doc;
    invalid.nodes[index].source = SourceRange{doc.source.size() + 1, 6};
    QVERIFY(!replaceText(invalid, index, "x").source);
    invalid.nodes[index].source = std::nullopt;
    QVERIFY(!replaceText(invalid, index, "x").source);
    invalid.nodes[index].source = SourceRange{-1, 6};
    QVERIFY(!replaceText(invalid, index, "x").source);
    QCOMPARE(doc.source, QByteArray("_before_\n"));
  }

  void emptyAndInvalidInput() {
    auto empty = parse({});
    QVERIFY(empty.document);
    QVERIFY(empty.document->source.isEmpty());
    QVERIFY(!signature(*empty.document).isEmpty());
    for (const auto& source :
         {QByteArray(1, char(0xff)), QByteArray(1, char(0xc3))}) {
      auto invalid = parse(source);
      QVERIFY(!invalid.document);
      QVERIFY(!invalid.error.isEmpty());
    }
  }

  void noImplicitExtensions() {
    auto parsed = parse(
        "==highlight== $math$ [[wiki]]\n\n> [!NOTE]\n> text\n\n"
        "[^a]: footnote\n\nreference[^a]\n");
    QVERIFY(parsed.document);
    for (const auto& node : parsed.document->nodes) {
      if (node.kind == NodeKind::kSpan) {
        QVERIFY(node.type != MD_SPAN_WIKILINK);
        QVERIFY(node.type != MD_SPAN_U);
      }
      if (node.kind == NodeKind::kBlock) {
        QVERIFY(node.type != MD_BLOCK_ADMONITION);
      }
    }
    qInfo("Only explicit GFM flags enabled, not MD_DIALECT_GITHUB extras");
  }

  void structuralEdits_data() {
    QTest::addColumn<QByteArray>("block");
    QTest::addColumn<QByteArray>("normalized");
    QTest::addColumn<int>("level");
    QTest::newRow("paragraph") << QByteArray("plain **bold**\n")
                               << QByteArray("## plain **bold**\n") << 2;
    QTest::newRow("heading-level")
        << QByteArray("# _italic_\n") << QByteArray("###### _italic_\n") << 6;
    QTest::newRow("demote")
        << QByteArray("### text ###\n") << QByteArray("text\n") << 0;
    QTest::newRow("indent-close-crlf")
        << QByteArray("  ## text  ## \r\n") << QByteArray("# text\r\n") << 1;
    QTest::newRow("unicode")
        << QStringLiteral(
               "## \u041f\u0440\u0438\u0432\u0435\u0442 \U0001f600\n")
               .toUtf8()
        << QStringLiteral(
               "### \u041f\u0440\u0438\u0432\u0435\u0442 \U0001f600\n")
               .toUtf8()
        << 3;
    QTest::newRow("escaped-hash")
        << QByteArray("# text \\#\n") << QByteArray("## text \\#\n") << 2;
    QTest::newRow("eof") << QByteArray("plain") << QByteArray("# plain") << 1;
    QTest::newRow("inline-code-html")
        << QByteArray("# `code` <span data-k=\"v\">text</span>\n")
        << QByteArray("## `code` <span data-k=\"v\">text</span>\n") << 2;
  }

  void structuralEdits() {
    QFETCH(QByteArray, block);
    QFETCH(QByteArray, normalized);
    QFETCH(int, level);
    const QByteArray prefix = "<!-- keep -->\r\n\r\n";
    const QByteArray suffix =
        block.endsWith('\n') ? QByteArray("\r\n~~~cpp\r\nsame();\r\n~~~\r\n")
                             : QByteArray{};
    const auto source = prefix + block + suffix;
    auto parsed = parse(source, 7);
    QVERIFY2(parsed.document, qPrintable(parsed.error));
    const auto& doc = *parsed.document;
    const auto map = mapBlocks(doc);
    QCOMPARE(map.size(), size_t{1});
    QCOMPARE(map[0].range.offset, prefix.size());
    QCOMPARE(source.mid(map[0].range.offset, map[0].range.size), block);
    auto result = setHeadingLevel(doc, snapshotToken(doc), map[0].node, level);
    QVERIFY2(result.document, qPrintable(result.error));
    QCOMPARE(result.document->source, prefix + normalized + suffix);
    QCOMPARE(result.document->revision, quint64{8});
    QCOMPARE(doc.source, source);
    QCOMPARE(doc.revision, quint64{7});
    const auto& changed = result.document->nodes[map[0].node];
    QCOMPARE(changed.type, level == 0 ? int(MD_BLOCK_P) : int(MD_BLOCK_H));
    if (level > 0) {
      QCOMPARE(changed.attributes["level"].toInt(), level);
    }
    auto stale = setHeadingLevel(*result.document, snapshotToken(doc),
                                 map[0].node, level);
    QVERIFY(!stale.document);
    QVERIFY(stale.error.contains("snapshot"));
  }

  void blockMappingRejectsUnsupportedForms() {
    for (const auto& source :
         {QByteArray("setext\n======\n"), QByteArray("multi\nline\n"),
          QByteArray("> lazy\ncontinuation\n"), QByteArray("- # listed\n"),
          QByteArray("```\ncode\n```\n"), QByteArray("<div>raw</div>\n"),
          QByteArray("#\n"), QByteArray("[link][id]\n\n[id]: /target\n")}) {
      auto parsed = parse(source);
      QVERIFY2(parsed.document, qPrintable(parsed.error));
      QVERIFY2(mapBlocks(*parsed.document).empty(), source.constData());
      const auto index = parsed.document->nodes[0].children.front();
      const auto result = setHeadingLevel(
          *parsed.document, snapshotToken(*parsed.document), index, 2);
      QVERIFY(!result.document);
      QVERIFY(!result.error.isEmpty());
      QCOMPARE(parsed.document->source, source);
    }
  }

  void versionedEditsAndRepeatedBlocks() {
    auto parsed = parse("same\n\nsame\n", 12);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto map = mapBlocks(doc);
    QCOMPARE(map.size(), size_t{2});
    auto changed = setHeadingLevel(doc, snapshotToken(doc), map[1].node, 1);
    QVERIFY2(changed.document, qPrintable(changed.error));
    QCOMPARE(changed.document->source, QByteArray("same\n\n# same\n"));
    auto nextMap = mapBlocks(*changed.document);
    QCOMPARE(nextMap.size(), size_t{2});
    auto reversed =
        setHeadingLevel(*changed.document, snapshotToken(*changed.document),
                        nextMap[1].node, 0);
    QVERIFY2(reversed.document, qPrintable(reversed.error));
    QCOMPARE(reversed.document->source, doc.source);
    QCOMPARE(reversed.document->revision, quint64{14});
    auto aba =
        setHeadingLevel(*reversed.document, snapshotToken(doc), map[1].node, 1);
    QVERIFY(!aba.document);

    auto foreign = parse("other\n\nsame\n", 12);
    QVERIFY(foreign.document);
    QVERIFY(
        !setHeadingLevel(doc, snapshotToken(*foreign.document), map[0].node, 1)
             .document);
    for (int level : {-1, 7}) {
      auto result =
          setHeadingLevel(doc, snapshotToken(doc), map[0].node, level);
      QVERIFY(!result.document);
      QVERIFY(!result.error.isEmpty());
    }
    QVERIFY(!setHeadingLevel(doc, snapshotToken(doc), doc.nodes.size(), 1)
                 .document);
    auto noop = setHeadingLevel(doc, snapshotToken(doc), map[0].node, 0);
    QVERIFY(noop.document);
    QCOMPARE(noop.document->source, doc.source);
    QCOMPARE(noop.document->revision, doc.revision);
    auto exhausted = parse("text\n", std::numeric_limits<quint64>::max());
    QVERIFY(exhausted.document);
    auto overflow =
        setHeadingLevel(*exhausted.document, snapshotToken(*exhausted.document),
                        mapBlocks(*exhausted.document)[0].node, 1);
    QVERIFY(!overflow.document);
    QVERIFY(overflow.error.contains("overflow"));
    auto revisionOnly = snapshotToken(doc);
    --revisionOnly.revision;
    auto staleRevision = setHeadingLevel(doc, revisionOnly, map[0].node, 1);
    QVERIFY(!staleRevision.document);
    QVERIFY(staleRevision.error.contains("snapshot"));
  }

  void allHeadingLevels() {
    auto parsed = parse("paragraph\n\n[id]: /unchanged \"title\"\n", 1);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto map = mapBlocks(doc);
    QCOMPARE(map.size(), size_t{1});
    for (int level = 1; level <= 6; ++level) {
      auto promoted =
          setHeadingLevel(doc, snapshotToken(doc), map[0].node, level);
      QVERIFY2(promoted.document, qPrintable(promoted.error));
      QCOMPARE(promoted.document->source,
               QByteArray(level, '#') +
                   " paragraph\n\n[id]: /unchanged \"title\"\n");
      const auto promotedMap = mapBlocks(*promoted.document);
      QCOMPARE(promotedMap.size(), size_t{1});
      auto demoted =
          setHeadingLevel(*promoted.document, snapshotToken(*promoted.document),
                          promotedMap[0].node, 0);
      QVERIFY2(demoted.document, qPrintable(demoted.error));
      QCOMPARE(demoted.document->source, doc.source);
      QCOMPARE(demoted.document->revision, quint64{3});
    }
  }

  void structuralEditRejectsContextChanges() {
    // Demoting a heading would turn the next thematic break into Setext syntax.
    auto parsed = parse("# title\n---\n");
    QVERIFY(parsed.document);
    const auto map = mapBlocks(*parsed.document);
    QCOMPARE(map.size(), size_t{1});
    auto result = setHeadingLevel(
        *parsed.document, snapshotToken(*parsed.document), map[0].node, 0);
    QVERIFY(!result.document);
    QVERIFY(result.error.contains("semantics"));
    QCOMPARE(parsed.document->source, QByteArray("# title\n---\n"));

    auto noop = setHeadingLevel(
        *parsed.document, snapshotToken(*parsed.document), map[0].node, 1);
    QVERIFY(noop.document);
    QCOMPARE(noop.document->source, parsed.document->source);
    QCOMPARE(noop.document->revision, parsed.document->revision);
  }

  void nestedQuoteEdits_data() {
    QTest::addColumn<QByteArray>("prefix");
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QByteArray>("ending");
    QTest::addColumn<int>("level");
    QTest::newRow("paragraph")
        << QByteArray("> ") << QByteArray("same") << QByteArray("\n") << 2;
    QTest::newRow("nested")
        << QByteArray("> > ") << QByteArray("same") << QByteArray("\r\n") << 3;
    QTest::newRow("compact")
        << QByteArray(">>>") << QByteArray("# same") << QByteArray() << 0;
    QTest::newRow("indented")
        << QByteArray("  >   > ") << QByteArray("## **same** ##")
        << QByteArray("\r\n") << 1;
    QTest::newRow("utf8") << QByteArray("> > > ")
                          << QStringLiteral(
                                 "_\u0442\u0435\u043a\u0441\u0442_ \U0001f600")
                                 .toUtf8()
                          << QByteArray() << 6;
  }

  void nestedQuoteEdits() {
    QFETCH(QByteArray, prefix);
    QFETCH(QByteArray, body);
    QFETCH(QByteArray, ending);
    QFETCH(int, level);
    const QByteArray before = "<!-- same -->\n\n";
    const QByteArray after = ending.isEmpty() ? QByteArray() : "\nlast\n";
    const auto source = before + prefix + body + ending + after;
    auto parsed = parse(source, 5);
    QVERIFY2(parsed.document, qPrintable(parsed.error));
    const auto& doc = *parsed.document;
    auto map = mapBlocks(doc);
    QVERIFY(!map.empty());
    const auto& block = map.front();
    QCOMPARE(block.range.offset, before.size() + prefix.size());
    QCOMPARE(block.range.size, body.size() + ending.size());
    const auto content = source.mid(block.content.offset, block.content.size);
    const auto replacement =
        (level ? QByteArray(level, '#') + ' ' : QByteArray()) + content +
        ending;
    auto edited =
        applyEdit(doc, snapshotToken(doc), SetHeadingLevel{block.node, level});
    QVERIFY2(edited.document, qPrintable(edited.error));
    QCOMPARE(edited.document->source, before + prefix + replacement + after);
    QCOMPARE(edited.document->revision, quint64{6});
    QCOMPARE(edited.document->identity, doc.identity);
    QVERIFY(!doc.nodes[0].parent);
    for (size_t index = 1; index < doc.nodes.size(); ++index) {
      const auto& node = doc.nodes[index];
      QVERIFY(node.parent);
      QVERIFY(*node.parent < index);
      const auto& children = doc.nodes[*node.parent].children;
      QVERIFY(std::find(children.begin(), children.end(), index) !=
              children.end());
    }
    for (const auto& envelope : evidenceEnvelopes(doc)) {
      QCOMPARE(envelope.parent, doc.nodes[envelope.node].parent);
      QVERIFY(envelope.range.offset >= 0);
      QVERIFY(envelope.range.size > 0);
      QVERIFY(envelope.range.offset + envelope.range.size <= source.size());
      auto ancestor = std::optional<size_t>(block.node);
      while (ancestor && *ancestor != 0) {
        if (*ancestor == envelope.node) {
          QVERIFY(envelope.range.offset >= before.size() + prefix.size());
          QVERIFY(envelope.range.offset != before.size());
        }
        ancestor = doc.nodes[*ancestor].parent;
      }
    }
    QCOMPARE(doc.source, source);
  }

  void nestedSiblingsAndContext() {
    auto parsed = parse("> same\n>\n> > same\n>\n> same\n");
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto map = mapBlocks(doc);
    QCOMPARE(map.size(), size_t{3});
    auto changed =
        applyEdit(doc, snapshotToken(doc), SetHeadingLevel{map[1].node, 2});
    QVERIFY2(changed.document, qPrintable(changed.error));
    QCOMPARE(changed.document->source,
             QByteArray("> same\n>\n> > ## same\n>\n> same\n"));
    const auto envelopes = evidenceEnvelopes(doc);
    for (const auto& child : envelopes) {
      if (!child.parent) {
        continue;
      }
      const auto parent = std::find_if(
          envelopes.begin(), envelopes.end(),
          [&](const auto& value) { return value.node == *child.parent; });
      QVERIFY(parent != envelopes.end());
      QVERIFY(parent->range.offset <= child.range.offset);
      QVERIFY(parent->range.offset + parent->range.size >=
              child.range.offset + child.range.size);
    }
    for (auto unsupported :
         {"> multi\n> line\n", "> multi\nlazy\n", "- > # heading\n",
          "> - # heading\n", "> [ref][id]\n\n[id]: /outside\n", "> #\n",
          "> title\n> ===\n"}) {
      auto result = parse(unsupported);
      QVERIFY(result.document);
      QVERIFY2(mapBlocks(*result.document).empty(), unsupported);
    }
    auto context = parse("> # title\n> ---\n");
    QVERIFY(context.document);
    const auto contextMap = mapBlocks(*context.document);
    QCOMPARE(contextMap.size(), size_t{1});
    auto rejected =
        applyEdit(*context.document, snapshotToken(*context.document),
                  SetHeadingLevel{contextMap[0].node, 0});
    QVERIFY(!rejected.document);
    QVERIFY(rejected.error.contains("semantics"));
  }

  void unifiedCommandContract() {
    auto parsed = parse("before\n\n- [x] task\n\n> quoted\n", 17);
    QVERIFY(parsed.document);
    const auto original = *parsed.document;
    const auto text = find(original, NodeKind::kText, MD_TEXT_NORMAL, "before");
    const auto task = find(original, NodeKind::kBlock, MD_BLOCK_LI);
    const auto heading = mapBlocks(original).back().node;
    const std::vector<EditCommand> commands{ReplaceText{text, "after"},
                                            SetTaskChecked{task, false},
                                            SetHeadingLevel{heading, 2}};
    const std::vector<EditCommand> noops{ReplaceText{text, "before"},
                                         SetTaskChecked{task, true},
                                         SetHeadingLevel{heading, 0}};
    const auto token = snapshotToken(original);
    auto foreign = parse(original.source, original.revision);
    QVERIFY(foreign.document);
    QVERIFY(foreign.document->identity != original.identity);
    auto invalidDigest = token;
    invalidDigest.digest[0] = char(invalidDigest.digest[0] ^ 1);
    auto invalidRevision = token;
    --invalidRevision.revision;
    auto invalidIdentity = token;
    invalidIdentity.identity = QUuid{};
    for (size_t index = 0; index < commands.size(); ++index) {
      for (const auto& invalid :
           {snapshotToken(*foreign.document), invalidDigest, invalidRevision,
            invalidIdentity}) {
        for (const auto& command : {commands[index], noops[index]}) {
          auto rejected = applyEdit(original, invalid, command);
          QVERIFY(!rejected.document);
          QVERIFY(rejected.error.contains("snapshot"));
        }
      }
      const auto copy = original;
      auto noop = applyEdit(copy, token, noops[index]);
      QVERIFY2(noop.document, qPrintable(noop.error));
      QCOMPARE(noop.document->source, original.source);
      QCOMPARE(noop.document->revision, original.revision);
      QCOMPARE(noop.document->identity, original.identity);
      auto edited = applyEdit(original, token, commands[index]);
      QVERIFY2(edited.document, qPrintable(edited.error));
      QCOMPARE(edited.document->revision, original.revision + 1);
      QCOMPARE(edited.document->identity, original.identity);
      QVERIFY(!applyEdit(*edited.document, token, noops[index]).document);
      auto reversed = applyEdit(*edited.document,
                                snapshotToken(*edited.document), noops[index]);
      QVERIFY2(reversed.document, qPrintable(reversed.error));
      QCOMPARE(reversed.document->source, original.source);
      QCOMPARE(reversed.document->revision, original.revision + 2);
      QVERIFY(!applyEdit(*reversed.document, token, commands[index]).document);

      auto exhausted =
          parse(original.source, std::numeric_limits<quint64>::max());
      QVERIFY(exhausted.document);
      auto overflow =
          applyEdit(*exhausted.document, snapshotToken(*exhausted.document),
                    commands[index]);
      QVERIFY(!overflow.document);
      QVERIFY(overflow.error.contains("overflow"));
      QVERIFY(applyEdit(*exhausted.document, snapshotToken(*exhausted.document),
                        noops[index])
                  .document);
    }
    auto current = original;
    for (const auto& command : commands) {
      auto edited = applyEdit(current, snapshotToken(current), command);
      QVERIFY2(edited.document, qPrintable(edited.error));
      current = std::move(*edited.document);
    }
    QCOMPARE(current.source,
             QByteArray("after\n\n- [ ] task\n\n> ## quoted\n"));
    QCOMPARE(current.revision, quint64{20});
    QCOMPARE(current.identity, original.identity);
    for (const auto& command :
         std::vector<EditCommand>{ReplaceText{original.nodes.size(), "text"},
                                  SetTaskChecked{original.nodes.size(), true},
                                  SetHeadingLevel{original.nodes.size(), 1}}) {
      const auto rejected = applyEdit(original, token, command);
      QVERIFY(!rejected.document);
      QVERIFY(!rejected.error.isEmpty());
    }
  }

  void evidenceIsNotContainerOwnership() {
    const QByteArray source = "> - [ ] item\n";
    auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto item = find(doc, NodeKind::kBlock, MD_BLOCK_LI);
    QVERIFY(item < doc.nodes.size());
    const auto envelopes = evidenceEnvelopes(doc);
    QVERIFY(!envelopes.empty());
    for (const auto& envelope : envelopes) {
      QCOMPARE(envelope.range.offset, source.indexOf("[ ]") + 1);
      QCOMPARE(envelope.range.offset + envelope.range.size,
               source.indexOf("item") + 4);
    }
    QVERIFY(std::any_of(envelopes.begin(), envelopes.end(),
                        [&](const auto& value) { return value.node == item; }));
    QVERIFY(mapBlocks(doc).empty());
    auto empty = parse("> #\n");
    QVERIFY(empty.document);
    QVERIFY(evidenceEnvelopes(*empty.document).empty());
  }
};

}  // namespace markpad::md4c_spike

QTEST_GUILESS_MAIN(markpad::md4c_spike::Md4cSourceTest)

#include "source_document_test.moc"
