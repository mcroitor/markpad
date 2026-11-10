#include <QtTest/QtTest>
#include <algorithm>
#include <limits>

#include "markdown_document.h"

namespace markpad::md4c_spike {
using namespace markpad::markdown;
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
  void referenceDefinitionScope_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<QStringList>("hrefs");
    QTest::addColumn<QStringList>("titles");
    const auto row = [](const char* name, const QByteArray& source,
                        const QStringList& hrefs, const QStringList& titles) {
      QTest::newRow(name) << source << hrefs << titles;
    };
    row("forward-definition", "[x][new]\n\n[new]: /new \"N\"\n", {"/new"},
        {"N"});
    row("definition-in-list-global",
        "[outside][new]\n\n- [new]: /new \"N\"\n\n  [inside][new]\n",
        {"/new", "/new"}, {"N", "N"});
    row("definition-in-quote-global",
        "[outside][new]\n\n> [new]: /new \"N\"\n>\n> text\n", {"/new"}, {"N"});
    row("first-wins-existing",
        "[new]: /old \"old\"\n\n- [new]: /new \"new\"\n\n  [x][new]\n",
        {"/old"}, {"old"});
    row("first-wins-insert-before-existing",
        "- [new]: /new \"new\"\n\n  [x][new]\n\n[new]: /old \"old\"\n\n"
        "[old][new]\n",
        {"/new", "/new"}, {"new", "new"});
    row("case-whitespace-collision",
        "[ A   B ]: /old\n[a b]: /new\n\n[x][a\t B]\n", {"/old"}, {""});
    row("unicode-fold",
        QStringLiteral("[STRASSE]: /old\n\n[x][stra\u00dfe]\n").toUtf8(),
        {"/old"}, {""});
    row("duplicate-fragment", "[new]: /first\n[new]: /second\n\n[x][new]\n",
        {"/first"}, {""});
    row("external-reference", "[outer]: /keep \"K\"\n\n- [x][outer]\n",
        {"/keep"}, {"K"});
    row("unresolved-before-insert", "[x][new]\n\n- base\n", {}, {});
    row("resolved-after-insert",
        "[x][new]\n\n- base\n- [new]: /new\n\n  text\n", {"/new"}, {""});
    row("definition-only", "[new]: /new \"N\"\n", {}, {});
    row("definition-with-paragraph", "[new]: /new\n\nparagraph\n", {}, {});
    row("before-removing-definition-item",
        "- [new]: /new \"N\"\n\n  owner\n- [sibling][new]\n", {"/new"}, {"N"});
    row("after-removing-definition-item", "- [sibling][new]\n", {}, {});
    row("fallback-after-removing-first",
        "[new]: /fallback \"F\"\n\n- [sibling][new]\n", {"/fallback"}, {"F"});
    row("table-reference",
        "[new]: /new \"N\"\n\n| h |\n| --- |\n| [cell][new] |\n", {"/new"},
        {"N"});
    row("image-reference", "![alt][new]\n\n[new]: /image \"I\"\n", {}, {});
    row("multiline-definition", "[new]:\n  /new\n  \"multi\"\n\n[x][new]\n",
        {"/new"}, {"multi"});
    row("multiline-label", "[a\n b]: /new\n\n[x][A B]\n", {"/new"}, {""});
    row("definition-looking-code",
        "```\n[new]: /not-a-definition\n```\n\n[x][new]\n", {}, {});
    row("unique-definition-insert-result",
        "- base\n- [new]: /new \"N\"\n\n  [x][new]\n", {"/new"}, {"N"});
    row("external-reference-insert-result",
        "[outer]: /keep \"K\"\n\n- base\n- [x][outer]\n", {"/keep"}, {"K"});
    row("nested-definition-insert-result",
        "> - a\n> - [new]: /new \"N\"\n>\n>   [x][new]\n", {"/new"}, {"N"});
    row("unicode-whitespace",
        QStringLiteral("[a b]: /keep\n\n[x][a\u00a0b]\n").toUtf8(), {"/keep"},
        {""});
    row("collapsed-shortcut", "[new][] [new]\n\n[new]: /new \"N\"\n",
        {"/new", "/new"}, {"N", "N"});
    row("same-target-duplicates",
        "[new]: /keep \"K\"\n[NEW]: /keep \"K\"\n\n[x][new]\n", {"/keep"},
        {"K"});
  }

  void referenceDefinitionScope() {
    QFETCH(QByteArray, source);
    QFETCH(QStringList, hrefs);
    QFETCH(QStringList, titles);
    for (bool crlf : {false, true}) {
      auto bytes = source;
      if (crlf) {
        bytes.replace("\n", "\r\n");
      }
      const auto parsed = parse(bytes);
      QVERIFY2(parsed.document, qPrintable(parsed.error));
      QStringList actualHrefs;
      QStringList actualTitles;
      for (const auto& node : parsed.document->nodes) {
        if (node.kind == NodeKind::kSpan && node.type == MD_SPAN_A) {
          actualHrefs.append(node.attributes["href"].toString());
          actualTitles.append(node.attributes["title"].toString());
          QCOMPARE(node.attributes["autolink"].toBool(), false);
        }
      }
      QCOMPARE(actualHrefs, hrefs);
      QCOMPARE(actualTitles, titles);
      QCOMPARE(parsed.document->source, bytes);
    }
  }

  void referenceDefinitionSourceOnly() {
    const auto only = parse("[new]: /new \"N\"\n");
    QVERIFY(only.document);
    QCOMPARE(only.document->nodes.size(), size_t(1));
    QVERIFY(only.document->nodes[0].children.empty());
    const auto mixed = parse("[new]: /new \"N\"\n\nparagraph\n");
    QVERIFY(mixed.document);
    QCOMPARE(mixed.document->nodes[0].children.size(), size_t(1));
    const auto paragraph = mixed.document->nodes[0].children[0];
    QCOMPARE(mixed.document->nodes[paragraph].type, int(MD_BLOCK_P));
    const auto text =
        find(*mixed.document, NodeKind::kText, MD_TEXT_NORMAL, "paragraph");
    QVERIFY(text < mixed.document->nodes.size());
    QVERIFY(mixed.document->nodes[text].source);
    QCOMPARE(mixed.document->nodes[text].source->offset, qsizetype(17));
    const auto image = parse("![alt][new]\n\n[new]: /image \"I\"\n");
    QVERIFY(image.document);
    const auto span = find(*image.document, NodeKind::kSpan, MD_SPAN_IMG);
    QVERIFY(span < image.document->nodes.size());
    QCOMPARE(image.document->nodes[span].attributes["src"].toString(),
             QStringLiteral("/image"));
    QCOMPARE(image.document->nodes[span].attributes["title"].toString(),
             QStringLiteral("I"));
  }

  void listSourceRanges_data() {
    QTest::addColumn<QByteArray>("list");
    QTest::addColumn<QList<int>>("offsets");
    QTest::addColumn<QByteArrayList>("items");
    QTest::addColumn<QByteArrayList>("contents");
    QTest::newRow("two-items")
        << QByteArray("- a\n- b\n") << QList<int>{0, 4}
        << QByteArrayList{"- a\n", "- b\n"} << QByteArrayList{"a\n", "b\n"};
    QTest::newRow("one-item")
        << QByteArray("- a\n") << QList<int>{0} << QByteArrayList{"- a\n"}
        << QByteArrayList{"a\n"};
    QTest::newRow("loose") << QByteArray("- a\n\n- b\n") << QList<int>{0, 5}
                           << QByteArrayList{"- a\n", "- b\n"}
                           << QByteArrayList{"a\n", "b\n"};
    QTest::newRow("multiline")
        << QByteArray("- first\n  second\n") << QList<int>{0}
        << QByteArrayList{"- first\n  second\n"}
        << QByteArrayList{"first\nsecond\n"};
    QTest::newRow("fenced-code")
        << QByteArray("- a\n\n  ```txt\n  x\n  ```\n") << QList<int>{0}
        << QByteArrayList{"- a\n\n  ```txt\n  x\n  ```\n"}
        << QByteArrayList{"a\n\n```txt\nx\n```\n"};
    QTest::newRow("inline-html")
        << QByteArray("- <i>x</i>\n") << QList<int>{0}
        << QByteArrayList{"- <i>x</i>\n"} << QByteArrayList{"<i>x</i>\n"};
    QTest::newRow("ordered")
        << QByteArray("3. a\n9. b\n") << QList<int>{0, 5}
        << QByteArrayList{"3. a\n", "9. b\n"} << QByteArrayList{"a\n", "b\n"};
    QTest::newRow("tasks") << QByteArray("- [ ] a\n- [X] b\n")
                           << QList<int>{0, 8}
                           << QByteArrayList{"- [ ] a\n", "- [X] b\n"}
                           << QByteArrayList{"[ ] a\n", "[X] b\n"};
    QTest::newRow("code-markers-and-tab")
        << QByteArray("- ~~~txt\n  - literal\n  \tcode\n  ~~~\n- b\n")
        << QList<int>{0, 35}
        << QByteArrayList{"- ~~~txt\n  - literal\n  \tcode\n  ~~~\n", "- b\n"}
        << QByteArrayList{"~~~txt\n- literal\n\tcode\n~~~\n", "b\n"};
    QTest::newRow("indent")
        << QByteArray("  - a\n    second\n  - b\n") << QList<int>{0, 17}
        << QByteArrayList{"  - a\n    second\n", "  - b\n"}
        << QByteArrayList{"a\nsecond\n", "b\n"};
    QTest::newRow("inline-link")
        << QByteArray("- [a](/keep)\n- **b**\n") << QList<int>{0, 13}
        << QByteArrayList{"- [a](/keep)\n", "- **b**\n"}
        << QByteArrayList{"[a](/keep)\n", "**b**\n"};
    const auto emoji = QStringLiteral("\u0430\U0001f600").toUtf8();
    QTest::newRow("unicode") << QByteArray("- " + emoji + "\n- b\n")
                             << QList<int>{0, int(emoji.size()) + 3}
                             << QByteArrayList{"- " + emoji + "\n", "- b\n"}
                             << QByteArrayList{emoji + "\n", "b\n"};
  }

  void listSourceRanges() {
    QFETCH(QByteArray, list);
    QFETCH(QList<int>, offsets);
    QFETCH(QByteArrayList, items);
    QFETCH(QByteArrayList, contents);
    QCOMPARE(items.size(), offsets.size());
    QCOMPARE(items.size(), contents.size());
    for (bool unicode : {false, true}) {
      for (bool crlf : {false, true}) {
        for (bool eof : {false, true}) {
          const auto eol = [crlf, unicode](QByteArray text) {
            if (unicode) {
              const auto value = QStringLiteral("\u0430\U0001f600").toUtf8();
              text.replace("a", value);
              text.replace("b", value);
              text.replace("x", value);
              text.replace("first", value);
            }
            return crlf ? text.replace("\n", "\r\n") : text;
          };
          const auto ending = crlf ? QByteArray("\r\n") : QByteArray("\n");
          auto rawList = eol(list);
          if (eof) {
            rawList.chop(ending.size());
          }
          const auto prefix = eol("pre\n\n");
          const auto suffix = eof ? QByteArray{} : eol("\npost\n");
          const auto unit = prefix + rawList + suffix;
          // Repeat the exact same bytes. Text matching cannot distinguish ids.
          const auto source = eof ? unit : unit + ending + ending + unit;
          const auto parsed = parse(source, 9);
          QVERIFY2(parsed.document, qPrintable(parsed.error));
          const auto& doc = *parsed.document;
          const auto originalSignature = signature(doc);
          std::vector<size_t> lists;
          for (auto child : doc.nodes[0].children) {
            if (doc.nodes[child].type == MD_BLOCK_UL ||
                doc.nodes[child].type == MD_BLOCK_OL) {
              lists.push_back(child);
            }
          }
          QCOMPARE(lists.size(), eof ? size_t(1) : size_t(2));
          for (size_t duplicate = 0; duplicate < lists.size(); ++duplicate) {
            const auto result = mapList(doc, lists[duplicate]);
            QVERIFY2(result.list, qPrintable(result.error));
            QVERIFY(result.error.isEmpty());
            const auto& mapped = *result.list;
            const auto start =
                prefix.size() +
                qsizetype(duplicate) * (unit.size() + 2 * ending.size());
            QCOMPARE(mapped.node, lists[duplicate]);
            QCOMPARE(mapped.range.offset, start);
            QCOMPARE(mapped.range.size, rawList.size());
            QCOMPARE(source.mid(mapped.range.offset, mapped.range.size),
                     rawList);
            QCOMPARE(mapped.items.size(), size_t(items.size()));
            for (qsizetype i = 0; i < items.size(); ++i) {
              const auto& item = mapped.items[size_t(i)];
              auto rawItem = eol(items[i]);
              auto content = eol(contents[i]);
              if (eof && i == items.size() - 1) {
                rawItem.chop(ending.size());
                content.chop(ending.size());
              }
              QCOMPARE(item.node,
                       doc.nodes[lists[duplicate]].children[size_t(i)]);
              QCOMPARE(doc.nodes[item.node].parent,
                       std::optional<size_t>(lists[duplicate]));
              QCOMPARE(item.range.offset,
                       start + eol(list.left(offsets[i])).size());
              QCOMPARE(item.range.size, rawItem.size());
              QCOMPARE(source.mid(item.range.offset, item.range.size), rawItem);
              for (const auto& evidence : evidenceEnvelopes(doc)) {
                if (evidence.node == item.node) {
                  QVERIFY(evidence.range.offset >= item.range.offset);
                  QVERIFY(evidence.range.offset + evidence.range.size <=
                          item.range.offset + item.range.size);
                }
              }
              QByteArray actualContent;
              qsizetype previous = item.range.offset;
              for (auto line : item.contentLines) {
                QVERIFY(line.offset >= previous);
                QVERIFY(line.offset + line.size <=
                        item.range.offset + item.range.size);
                actualContent += source.mid(line.offset, line.size);
                previous = line.offset + line.size;
              }
              QCOMPARE(actualContent, content);
            }
          }
          QCOMPARE(doc.source, source);
          QCOMPARE(doc.revision, quint64(9));
          QCOMPARE(signature(doc), originalSignature);
        }
      }
    }
  }

  void listSourceRejected_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::newRow("lazy") << QByteArray("- a\nlazy\n");
    QTest::newRow("structural-tab") << QByteArray("- a\n\tcontinuation\n");
    QTest::newRow("unclosed-fence") << QByteArray("- ```\n  open\n");
    QTest::newRow("indented-code") << QByteArray("- a\n\n      code\n");
    QTest::newRow("tab-after-marker") << QByteArray("-\ta\n");
    QTest::newRow("noncanonical-marker") << QByteArray("-  a\n");
    QTest::newRow("tab-in-continuation-prefix")
        << QByteArray("- a\n  \tcontinuation\n");
    QTest::newRow("tab-after-extra-spaces")
        << QByteArray("- a\n   \tcontinuation\n");
  }

  void listSourceRejected() {
    QFETCH(QByteArray, source);
    const auto parsed = parse(source);
    QVERIFY2(parsed.document, qPrintable(parsed.error));
    const auto& doc = *parsed.document;
    const auto list = find(doc, NodeKind::kBlock, MD_BLOCK_UL);
    QVERIFY(list < doc.nodes.size());
    const auto result = mapList(doc, list);
    QVERIFY(!result.list);
    QVERIFY2(result.error.startsWith("Unsupported"), qPrintable(result.error));
    QCOMPARE(doc.source, source);
    QCOMPARE(doc.revision, quint64(0));
  }

  void listSourceInvalidTargets() {
    const auto parsed = parse("pre\n\n- a\n- b\n\npost\n");
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto result = mapList(doc, find(doc, NodeKind::kBlock, MD_BLOCK_UL));
    QVERIFY2(result.list, qPrintable(result.error));
    QCOMPARE(result.list->range.offset, qsizetype(5));
    QCOMPARE(result.list->range.size, qsizetype(8));
    for (auto node : {size_t(0), size_t(1), doc.nodes.size(),
                      std::numeric_limits<size_t>::max()}) {
      const auto invalid = mapList(doc, node);
      QVERIFY(!invalid.list);
      QCOMPARE(invalid.error, QStringLiteral("Invalid list target"));
    }
    const auto empty = parse("");
    QVERIFY(empty.document);
    QVERIFY(!mapList(*empty.document, 0).list);
  }

  void listSourceOwnership() {
    const auto parsed = parse("pre\n\n- a\n- b\n\npost\n");
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto node = find(doc, NodeKind::kBlock, MD_BLOCK_UL);
    const auto mapped = mapList(doc, node);
    QVERIFY(mapped.list);
    const auto& items = mapped.list->items;
    QCOMPARE(items.size(), size_t(2));
    QCOMPARE(items[0].range.offset, qsizetype(5));
    QCOMPARE(items[0].range.size, qsizetype(4));
    QCOMPARE(items[1].range.offset, qsizetype(9));
    QCOMPARE(items[1].range.size, qsizetype(4));
    QCOMPARE(items[0].contentLines.size(), size_t(1));
    QCOMPARE(items[1].contentLines.size(), size_t(1));
    QCOMPARE(items[0].contentLines[0].offset, qsizetype(7));
    QCOMPARE(items[0].contentLines[0].size, qsizetype(2));
    QCOMPARE(items[1].contentLines[0].offset, qsizetype(11));
    QCOMPARE(items[1].contentLines[0].size, qsizetype(2));
    const auto fenced = parse("- a\n\n  ```txt\n  x\n  ```\n\npost\n");
    QVERIFY(fenced.document);
    const auto code =
        mapList(*fenced.document,
                find(*fenced.document, NodeKind::kBlock, MD_BLOCK_UL));
    QVERIFY2(code.list, qPrintable(code.error));
    QCOMPARE(code.list->range.offset, qsizetype(0));
    QCOMPARE(code.list->range.size, qsizetype(24));
    const auto& lines = code.list->items[0].contentLines;
    const std::vector<SourceRange> expected{
        {2, 2}, {4, 1}, {7, 7}, {16, 2}, {20, 4}};
    QCOMPARE(lines.size(), expected.size());
    for (size_t i = 0; i < lines.size(); ++i) {
      QCOMPARE(lines[i].offset, expected[i].offset);
      QCOMPARE(lines[i].size, expected[i].size);
    }
    const auto loose = parse("- a\n\n- b\n\npost\n");
    QVERIFY(loose.document);
    const auto gap = mapList(
        *loose.document, find(*loose.document, NodeKind::kBlock, MD_BLOCK_UL));
    QVERIFY(gap.list);
    QCOMPARE(gap.list->range.size, qsizetype(9));
    QCOMPARE(gap.list->items[0].range.size, qsizetype(4));
    QCOMPARE(gap.list->items[1].range.offset, qsizetype(5));
    QCOMPARE(gap.list->items[1].range.size, qsizetype(4));
  }

  void listSourceExternalContext() {
    const QByteArray prefix =
        "```\n- same\n```\n\n<div>\n- same\n</div>\n\n"
        "[id]: /keep\n\n> quote\nlazy\n\n";
    const QByteArray container = "- same\n\n- same\n";
    const QByteArray suffix = "\npost\n\n- other\nlazy\n";
    const auto parsed = parse(prefix + container + suffix);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto node = find(doc, NodeKind::kBlock, MD_BLOCK_UL);
    const auto result = mapList(doc, node);
    QVERIFY2(result.list, qPrintable(result.error));
    QCOMPARE(result.list->range.offset, prefix.size());
    QCOMPARE(result.list->range.size, container.size());
    QCOMPARE(doc.source.left(result.list->range.offset), prefix);
    QCOMPARE(
        doc.source.mid(result.list->range.offset + result.list->range.size),
        suffix);
  }

  void listSourceDefinitions() {
    const QByteArray source = "- a\n\n  [id]: /keep\n\n  b\n";
    const auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto list = find(doc, NodeKind::kBlock, MD_BLOCK_UL);
    const auto mapped = mapList(doc, list);
    QVERIFY2(mapped.list, qPrintable(mapped.error));
    QCOMPARE(mapped.list->items.size(), size_t(1));
    QCOMPARE(doc.source.mid(mapped.list->range.offset, mapped.list->range.size),
             source);
    QCOMPARE(mapped.list->items[0].contentLines.size(), size_t(5));

    const QByteArray external = "[id]: /keep\n\n- [a][id]\n";
    const auto externalParsed = parse(external);
    QVERIFY(externalParsed.document);
    const auto& externalDoc = *externalParsed.document;
    const auto externalList = find(externalDoc, NodeKind::kBlock, MD_BLOCK_UL);
    const auto externalMap = mapList(externalDoc, externalList);
    QVERIFY2(externalMap.list, qPrintable(externalMap.error));
    QCOMPARE(externalMap.list->range.offset, qsizetype(external.indexOf('-')));
    const auto link = find(externalDoc, NodeKind::kSpan, MD_SPAN_A);
    QVERIFY(link < externalDoc.nodes.size());
    QCOMPARE(externalDoc.nodes[link].attributes["href"].toString(),
             QStringLiteral("/keep"));

    const QByteArray forward = "- [a][after]\n\n[after]: /later \"L\"\n";
    const auto forwardParsed = parse(forward);
    QVERIFY(forwardParsed.document);
    const auto& forwardDoc = *forwardParsed.document;
    const auto forwardList = find(forwardDoc, NodeKind::kBlock, MD_BLOCK_UL);
    const auto forwardMap = mapList(forwardDoc, forwardList);
    QVERIFY2(forwardMap.list, qPrintable(forwardMap.error));
    const auto forwardLink = find(forwardDoc, NodeKind::kSpan, MD_SPAN_A);
    QVERIFY(forwardLink < forwardDoc.nodes.size());
    QCOMPARE(forwardDoc.nodes[forwardLink].attributes["href"].toString(),
             QStringLiteral("/later"));
  }

  void linkLabels_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<QByteArray>("label");
    QTest::addColumn<QString>("href");
    QTest::addColumn<QString>("title");
    QTest::addColumn<bool>("autolink");
    QTest::addColumn<QByteArray>("expected");
    QTest::newRow("inline")
        << QByteArray("[before](/keep \"title\")\n") << QByteArray("before")
        << QStringLiteral("/keep") << QStringLiteral("title") << false
        << QByteArray("[after](/keep \"title\")\n");
    QTest::newRow("reference")
        << QByteArray("[before][id]\n\n[id]: /keep \"title\"\n")
        << QByteArray("before") << QStringLiteral("/keep")
        << QStringLiteral("title") << false
        << QByteArray("[after][id]\n\n[id]: /keep \"title\"\n");
    QTest::newRow("formatted")
        << QByteArray("[**before**](/keep)\n") << QByteArray("before")
        << QStringLiteral("/keep") << QString{} << false
        << QByteArray("[**after**](/keep)\n");
    QTest::newRow("collapsed")
        << QByteArray("[before][]\n\n[before]: /keep\n") << QByteArray("before")
        << QStringLiteral("/keep") << QString{} << false << QByteArray{};
    QTest::newRow("shortcut")
        << QByteArray("[before]\n\n[before]: /keep\n") << QByteArray("before")
        << QStringLiteral("/keep") << QString{} << false << QByteArray{};
    QTest::newRow("angle-http") << QByteArray("<https://invalid.test>\n")
                                << QByteArray("https://invalid.test")
                                << QStringLiteral("https://invalid.test")
                                << QString{} << true << QByteArray{};
    QTest::newRow("gfm-http") << QByteArray("https://invalid.test\n")
                              << QByteArray("https://invalid.test")
                              << QStringLiteral("https://invalid.test")
                              << QString{} << true << QByteArray{};
    QTest::newRow("gfm-www")
        << QByteArray("www.invalid.test\n") << QByteArray("www.invalid.test")
        << QStringLiteral("http://www.invalid.test") << QString{} << true
        << QByteArray{};
    QTest::newRow("email") << QByteArray("<user@invalid.test>\n")
                           << QByteArray("user@invalid.test")
                           << QStringLiteral("mailto:user@invalid.test")
                           << QString{} << true << QByteArray{};
  }

  void linkLabels() {
    QFETCH(QByteArray, source);
    QFETCH(QByteArray, label);
    QFETCH(QString, href);
    QFETCH(QString, title);
    QFETCH(bool, autolink);
    QFETCH(QByteArray, expected);
    const auto parsed = parse(source);
    QVERIFY2(parsed.document, qPrintable(parsed.error));
    const auto& doc = *parsed.document;
    const auto link = find(doc, NodeKind::kSpan, MD_SPAN_A);
    QVERIFY(link < doc.nodes.size());
    QCOMPARE(doc.nodes[link].attributes["href"].toString(), href);
    QCOMPARE(doc.nodes[link].attributes["title"].toString(), title);
    QCOMPARE(doc.nodes[link].attributes["autolink"].toBool(), autolink);
    const auto node = find(doc, NodeKind::kText, MD_TEXT_NORMAL, label);
    QVERIFY(node < doc.nodes.size());
    const auto result =
        applyEdit(doc, snapshotToken(doc), ReplaceText{node, "after"});
    if (expected.isEmpty()) {
      QVERIFY(!result.document);
      QVERIFY(!result.error.isEmpty());
    } else {
      QVERIFY2(result.document, qPrintable(result.error));
      QCOMPARE(result.document->source, expected);
      const auto newLink = find(*result.document, NodeKind::kSpan, MD_SPAN_A);
      QVERIFY(newLink < result.document->nodes.size());
      QCOMPARE(result.document->nodes[newLink].attributes,
               doc.nodes[link].attributes);
    }
    QCOMPARE(doc.source, source);
    QCOMPARE(doc.revision, quint64(0));
  }

  void inlineStyle_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<QByteArray>("run");
    QTest::addColumn<int>("offset");
    QTest::addColumn<int>("size");
    QTest::addColumn<int>("style");
    QTest::addColumn<QByteArray>("expected");
    const auto add = [](const char* name, const QByteArray& source,
                        const QByteArray& run, int offset, int size,
                        InlineStyle style, const QByteArray& expected) {
      QTest::newRow(name) << source << run << offset << size << int(style)
                          << expected;
    };
    add("bold", "a word z\n", "a word z", 2, 4, InlineStyle::kBold,
        "a **word** z\n");
    add("italic", "a word z\n", "a word z", 2, 4, InlineStyle::kItalic,
        "a *word* z\n");
    add("strike", "a word z\n", "a word z", 2, 4, InlineStyle::kStrikethrough,
        "a ~~word~~ z\n");
    add("partial", "word\n", "word", 1, 2, InlineStyle::kBold, "w**or**d\n");
    add("unbold", "**word**\n", "word", 0, 4, InlineStyle::kBold, "word\n");
    add("unbold-underscore", "__word__\n", "word", 0, 4, InlineStyle::kBold,
        "word\n");
    add("unitalic", "*word*\n", "word", 0, 4, InlineStyle::kItalic, "word\n");
    add("unitalic-underscore", "_word_\n", "word", 0, 4, InlineStyle::kItalic,
        "word\n");
    add("unstrike", "~~word~~\n", "word", 0, 4, InlineStyle::kStrikethrough,
        "word\n");
    add("nested-bold", "_word_\n", "word", 0, 4, InlineStyle::kBold,
        "_**word**_\n");
    add("nested-strike", "**word**\n", "word", 0, 4,
        InlineStyle::kStrikethrough, "**~~word~~**\n");
    add("nested-remove", "**~~word~~**\n", "word", 0, 4,
        InlineStyle::kStrikethrough, "**word**\n");
    add("quote-list", "> - word\r\n", "word", 0, 4, InlineStyle::kBold,
        "> - **word**\r\n");
    add("eof", "word", "word", 0, 4, InlineStyle::kBold, "**word**");
    add("adjacent-normal", "a **word** z\n", "word", 0, 4, InlineStyle::kBold,
        "a word z\n");
    add("punctuation", "a!b\n", "a!b", 0, 3, InlineStyle::kBold, "**a!b**\n");
    const auto unicode =
        QStringLiteral("\u0441\u043b\u043e\u0432\u043e\U0001f600").toUtf8();
    add("unicode", "a " + unicode + " z\r\n", "a " + unicode + " z", 2,
        int(unicode.size()), InlineStyle::kBold, "a **" + unicode + "** z\r\n");
  }

  void inlineStyle() {
    QFETCH(QByteArray, source);
    QFETCH(QByteArray, run);
    QFETCH(int, offset);
    QFETCH(int, size);
    QFETCH(int, style);
    QFETCH(QByteArray, expected);
    const QByteArray prefix = "<div>opaque</div>\r\n\r\n";
    const QByteArray suffix = "\n\n[id]: /keep \"title\"\n";
    const auto parsed = parse(prefix + source + suffix, 4);
    QVERIFY2(parsed.document, qPrintable(parsed.error));
    const auto& doc = *parsed.document;
    const auto node = find(doc, NodeKind::kText, MD_TEXT_NORMAL, run);
    QVERIFY(node < doc.nodes.size());
    const auto command =
        ToggleInlineStyle{node, {offset, size}, InlineStyle(style)};
    const auto result = applyEdit(doc, snapshotToken(doc), command);
    QVERIFY2(result.document, qPrintable(result.error));
    QCOMPARE(result.document->source, prefix + expected + suffix);
    QCOMPARE(result.document->identity, doc.identity);
    QCOMPARE(result.document->revision, quint64(5));
    QCOMPARE(doc.source, prefix + source + suffix);
    const auto expectedTree = parse(prefix + expected + suffix);
    QVERIFY(expectedTree.document);
    QCOMPARE(signature(*result.document), signature(*expectedTree.document));
    QVERIFY(!applyEdit(*result.document, snapshotToken(doc), command).document);
  }

  void inlineStyleRejected_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<QByteArray>("run");
    QTest::addColumn<int>("offset");
    QTest::addColumn<int>("size");
    QTest::addColumn<int>("style");
    const auto add = [](const char* name, const QByteArray& source,
                        const QByteArray& run, int offset, int size,
                        int style = 0) {
      QTest::newRow(name) << source << run << offset << size << style;
    };
    add("empty", "word\n", "word", 0, 0);
    add("negative", "word\n", "word", -1, 1);
    add("past-end", "word\n", "word", 4, 1);
    add("oversize", "word\n", "word", 1, std::numeric_limits<int>::max());
    add("bad-style", "word\n", "word", 0, 4, 100);
    add("leading-space", "a word z\n", "a word z", 1, 5);
    add("trailing-space", "a word z\n", "a word z", 2, 5);
    add("partial-unbold", "**word**\n", "word", 1, 2);
    add("complex-unbold", "**word *inner***\n", "word ", 0, 4);
    add("ancestor-unbold", "**~~word~~**\n", "word", 0, 4);
    add("delimiter-collision", "**word**\n", "word", 0, 4,
        int(InlineStyle::kItalic));
    add("link", "[word](/keep)\n", "word", 0, 4);
    add("image", "![word](/keep)\n", "word", 0, 4);
    add("table", "| A |\n| - |\n| word |\n", "word", 0, 4);
    const auto emoji = QStringLiteral("\U0001f600").toUtf8();
    add("split-utf8-start", emoji + '\n', emoji, 1, 3);
    add("split-utf8-end", emoji + '\n', emoji, 0, 3);
  }

  void inlineStyleRejected() {
    QFETCH(QByteArray, source);
    QFETCH(QByteArray, run);
    QFETCH(int, offset);
    QFETCH(int, size);
    QFETCH(int, style);
    const auto parsed = parse(source);
    QVERIFY2(parsed.document, qPrintable(parsed.error));
    const auto& doc = *parsed.document;
    const auto node = find(doc, NodeKind::kText, MD_TEXT_NORMAL, run);
    QVERIFY(node < doc.nodes.size());
    const auto result =
        applyEdit(doc, snapshotToken(doc),
                  ToggleInlineStyle{node, {offset, size}, InlineStyle(style)});
    QVERIFY(!result.document);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(doc.source, source);
    QCOMPARE(doc.revision, quint64(0));
  }

  void inlineStyleGuards() {
    const auto parsed = parse("word\n");
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto node = find(doc, NodeKind::kText, MD_TEXT_NORMAL, "word");
    QVERIFY(node < doc.nodes.size());
    const auto command = ToggleInlineStyle{node, {0, 4}, InlineStyle::kBold};
    const auto foreign = parse("word\n");
    QVERIFY(foreign.document);
    QVERIFY(
        !applyEdit(doc, snapshotToken(*foreign.document), command).document);
    QVERIFY(!applyEdit(doc, snapshotToken(doc),
                       ToggleInlineStyle{
                           doc.nodes.size(), {0, 4}, InlineStyle::kBold})
                 .document);
    QVERIFY(
        !applyEdit(doc, snapshotToken(doc),
                   ToggleInlineStyle{node,
                                     {1, std::numeric_limits<qsizetype>::max()},
                                     InlineStyle::kBold})
             .document);
    auto token = snapshotToken(doc);
    ++token.revision;
    QVERIFY(!applyEdit(doc, token, command).document);
    token = snapshotToken(doc);
    token.digest = "wrong";
    QVERIFY(!applyEdit(doc, token, command).document);
    const auto code = parse("`word`\n");
    QVERIFY(code.document);
    const auto codeNode = find(*code.document, NodeKind::kText, MD_TEXT_CODE);
    QVERIFY(codeNode < code.document->nodes.size());
    QVERIFY(!applyEdit(*code.document, snapshotToken(*code.document),
                       ToggleInlineStyle{codeNode, {0, 4}, InlineStyle::kBold})
                 .document);
    QCOMPARE(doc.source, QByteArray("word\n"));
    QCOMPARE(doc.revision, quint64(0));
  }

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

  void textEditRejectsReferenceLinkRebinding() {
    auto parsed =
        parse("[before]\n\n[before]: /a&amp;b\n[after]: /a\\&amp;b\n");
    QVERIFY(parsed.document);
    const auto index =
        find(*parsed.document, NodeKind::kText, MD_TEXT_NORMAL, "before");
    QVERIFY(index < parsed.document->nodes.size());
    const auto result = replaceText(*parsed.document, index, "after");
    QVERIFY(!result.source);
    QVERIFY(result.error.contains("semantics"));
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

  void listInsert_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<int>("type");
    QTest::addColumn<int>("position");
    QTest::addColumn<QByteArray>("fragment");
    QTest::addColumn<QByteArray>("expected");
    const auto row = [](const char* name, const QByteArray& source, int type,
                        int position, const QByteArray& fragment,
                        const QByteArray& expected) {
      QTest::newRow(name) << source << type << position << fragment << expected;
    };
    row("ordered-middle", "3. a\n9. b\n", MD_BLOCK_OL, 1, "c",
        "3. a\n4. c\n5. b\n");
    row("ordered-append", "3. a\n", MD_BLOCK_OL, 1, "b", "3. a\n4. b\n");
    row("unordered-middle", "- a\n- b\n", MD_BLOCK_UL, 1, "c",
        "- a\n- c\n- b\n");
    row("multiblock", "- a\n- b\n", MD_BLOCK_UL, 1, "p\n\nq",
        "- a\n- p\n\n  q\n- b\n");
    row("loose-preserved", "- a\n\n- b\n", MD_BLOCK_UL, 1, "c",
        "- a\n\n- c\n- b\n");
    row("definition", "- base\n", MD_BLOCK_UL, 1,
        "[new]: /new \"N\"\n\n[x][new]",
        "- base\n- [new]: /new \"N\"\n\n  [x][new]\n");
    row("definition-crlf", "- base\r\n", MD_BLOCK_UL, 1,
        "[new]: /new\r\n\r\n[x][new]",
        "- base\r\n- [new]: /new\r\n\r\n  [x][new]\r\n");
    row("definition-eof", "- base", MD_BLOCK_UL, 1,
        "[new]: /new\n\n[x][new]",
        "- base\n- [new]: /new\n\n  [x][new]");
    row("external-reference", "[outer]: /keep \"K\"\n\n- base\n", MD_BLOCK_UL,
        1, "[x][outer]",
        "[outer]: /keep \"K\"\n\n- base\n- [x][outer]\n");
    row("task-like-literal", "- a\n", MD_BLOCK_UL, 1, "[ ] literal",
        "- a\n- \\[ ] literal\n");
    row("crlf", "3. a\r\n9. b\r\n", MD_BLOCK_OL, 1, "c",
        "3. a\r\n4. c\r\n5. b\r\n");
    row("eof", "- a", MD_BLOCK_UL, 1, "b", "- a\n- b");
    const auto emoji = QStringLiteral("\u0430\U0001f600").toUtf8();
    row("unicode", "- " + emoji + "\n", MD_BLOCK_UL, 1, emoji,
        "- " + emoji + "\n- " + emoji + "\n");
  }

  void listInsert() {
    QFETCH(QByteArray, source);
    QFETCH(int, type);
    QFETCH(int, position);
    QFETCH(QByteArray, fragment);
    QFETCH(QByteArray, expected);
    const auto parsed = parse(source);
    QVERIFY2(parsed.document, qPrintable(parsed.error));
    const auto& doc = *parsed.document;
    const auto list = find(doc, NodeKind::kBlock, type);
    QVERIFY(list < doc.nodes.size());
    const auto result =
        editBytes(doc, InsertListItem{list, size_t(position), fragment});
    QVERIFY2(result.source, qPrintable(result.error));
    QCOMPARE(*result.source, expected);
  }

  void listInsertRejected_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<QByteArray>("fragment");
    QTest::addColumn<QString>("error");
    QTest::newRow("empty") << QByteArray("- a\n") << QByteArray("")
                           << QStringLiteral("Invalid list item fragment");
    QTest::newRow("space") << QByteArray("- a\n") << QByteArray(" \n")
                           << QStringLiteral("Invalid list item fragment");
    QTest::newRow("definition-only") << QByteArray("- a\n")
                                     << QByteArray("[new]: /new\n")
                                     << QStringLiteral("Invalid list item fragment");
    QTest::newRow("overflow") << QByteArray("999999999. a\n") << QByteArray("b")
                              << QStringLiteral("overflow");
    QTest::newRow("definition-conflict")
        << QByteArray("[new]: /old\n\n- base\n")
        << QByteArray("[new]: /new \"N\"\n\n[x][new]")
        << QStringLiteral("Definition conflict");
    QTest::newRow("case-space-conflict")
        << QByteArray("[ A   B ]: /old\n\n- base\n")
        << QByteArray("[a b]: /new\n\n[x][A B]")
        << QStringLiteral("Definition conflict");
    QTest::newRow("fragment-duplicate")
        << QByteArray("- base\n")
        << QByteArray("[new]: /first\n[NEW]: /second\n\n[x][new]")
        << QStringLiteral("Definition conflict");
    QTest::newRow("binding-conflict")
        << QByteArray("[x][new]\n\n- base\n")
        << QByteArray("[new]: /new\n\n[x][new]")
        << QStringLiteral("Binding conflict");
  }

  void listInsertRejected() {
    QFETCH(QByteArray, source);
    QFETCH(QByteArray, fragment);
    QFETCH(QString, error);
    const auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    auto list = find(doc, NodeKind::kBlock, MD_BLOCK_UL);
    if (list >= doc.nodes.size()) {
      list = find(doc, NodeKind::kBlock, MD_BLOCK_OL);
    }
    QVERIFY(list < doc.nodes.size());
    const auto result = editBytes(doc, InsertListItem{list, 1, fragment});
    QVERIFY(!result.source);
    QVERIFY2(result.error.contains(error), qPrintable(result.error));
    QCOMPARE(doc.source, source);
    QCOMPARE(doc.revision, quint64(0));
  }

  void wrapInList_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<bool>("ordered");
    QTest::addColumn<int>("start");
    QTest::addColumn<QByteArray>("expected");
    QTest::newRow("unordered") << QByteArray("alpha\n") << false << 0
                               << QByteArray("- alpha\n");
    QTest::newRow("ordered") << QByteArray("alpha\n") << true << 1
                             << QByteArray("1. alpha\n");
    QTest::newRow("ordered-start") << QByteArray("alpha\n") << true << 3
                                   << QByteArray("3. alpha\n");
    QTest::newRow("multiline") << QByteArray("alpha\nbeta\n") << false << 0
                               << QByteArray("- alpha\n  beta\n");
    QTest::newRow("multiline-ordered")
        << QByteArray("alpha\nbeta\n") << true << 1
        << QByteArray("1. alpha\n   beta\n");
    QTest::newRow("crlf") << QByteArray("alpha\r\nbeta\r\n") << true << 1
                          << QByteArray("1. alpha\r\n   beta\r\n");
    QTest::newRow("eof") << QByteArray("alpha") << false << 0
                         << QByteArray("- alpha");
    QTest::newRow("reference")
        << QByteArray("[x][id]\n\n[id]: /keep \"K\"\n") << false << 0
        << QByteArray("- [x][id]\n\n[id]: /keep \"K\"\n");
  }

  void wrapInList() {
    QFETCH(QByteArray, source);
    QFETCH(bool, ordered);
    QFETCH(int, start);
    QFETCH(QByteArray, expected);
    const auto parsed = parse(source);
    QVERIFY2(parsed.document, qPrintable(parsed.error));
    const auto& doc = *parsed.document;
    QVERIFY(!doc.nodes[0].children.empty());
    const auto result = editBytes(doc, WrapInList{0, 0, ordered, start});
    QVERIFY2(result.source, qPrintable(result.error));
    QCOMPARE(*result.source, expected);
    const auto reparsed = parse(*result.source);
    QVERIFY(reparsed.document);
    const auto& root = reparsed.document->nodes[0];
    QCOMPARE(root.children.size(), size_t(1));
    const auto& list = reparsed.document->nodes[root.children.front()];
    QCOMPARE(int(list.type), ordered ? MD_BLOCK_OL : MD_BLOCK_UL);
    QCOMPARE(list.children.size(), size_t(1));
    QVERIFY(list.attributes["tight"].toBool());
    if (ordered) {
      QCOMPARE(list.attributes["start"].toInt(), start);
    }
    const auto& item = reparsed.document->nodes[list.children.front()];
    QCOMPARE(int(item.type), MD_BLOCK_LI);
    QVERIFY(!item.children.empty());

    const auto foreign = parse("other\n");
    QVERIFY(foreign.document);
    const auto rejected =
        applyEdit(doc, snapshotToken(*foreign.document),
                  WrapInList{0, 0, ordered, start});
    QVERIFY(!rejected.document);
    QVERIFY(!rejected.error.isEmpty());
  }

  void replaceCodeBody_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QByteArray>("expected");
    QTest::newRow("basic") << QByteArray("```\nold\n```\n")
                           << QByteArray("new\n")
                           << QByteArray("```\nnew\n```\n");
    QTest::newRow("info") << QByteArray("```js\nold\n```\n")
                          << QByteArray("new\n")
                          << QByteArray("```js\nnew\n```\n");
    QTest::newRow("crlf") << QByteArray("~~~\r\nold\r\n~~~\r\n")
                          << QByteArray("new\n")
                          << QByteArray("~~~\r\nnew\r\n~~~\r\n");
    QTest::newRow("append-eol") << QByteArray("```\nold\n```\n")
                                << QByteArray("new")
                                << QByteArray("```\nnew\n```\n");
    QTest::newRow("empty-body") << QByteArray("```\nold\n```\n")
                                << QByteArray("")
                                << QByteArray("```\n```\n");
    QTest::newRow("unclosed") << QByteArray("```\nold\n")
                              << QByteArray("new\n")
                              << QByteArray("```\nnew\n");
    QTest::newRow("outside") << QByteArray(
                                    "before\n\n```\nold\n```\n\nafter\n")
                             << QByteArray("new\n")
                             << QByteArray(
                                    "before\n\n```\nnew\n```\n\nafter\n");
    QTest::newRow("shorter-fence-in-body")
        << QByteArray("````\nold\n```\n````\n") << QByteArray("new\n")
        << QByteArray("````\nnew\n````\n");
    QTest::newRow("longer-close") << QByteArray("```\nold\n`````\n")
                                  << QByteArray("new\n")
                                  << QByteArray("```\nnew\n`````\n");
    QTest::newRow("blank-lines") << QByteArray("```\nold\n```\n")
                                 << QByteArray("one\n\ntwo\n")
                                 << QByteArray("```\none\n\ntwo\n```\n");
  }

  void replaceCodeBody() {
    QFETCH(QByteArray, source);
    QFETCH(QByteArray, body);
    QFETCH(QByteArray, expected);
    const auto parsed = parse(source);
    QVERIFY2(parsed.document, qPrintable(parsed.error));
    const auto& doc = *parsed.document;
    const auto code = find(doc, NodeKind::kBlock, MD_BLOCK_CODE);
    QVERIFY(code < doc.nodes.size());
    QString mapError;
    const auto mapped = mapFencedCodeBody(doc, code, mapError);
    QVERIFY2(mapped, qPrintable(mapError));
    QVERIFY(mapped->body.offset >= mapped->block.offset);
    QVERIFY(mapped->body.offset + mapped->body.size <=
            mapped->block.offset + mapped->block.size);
    const auto result = editBytes(doc, ReplaceCodeBody{code, body});
    QVERIFY2(result.source, qPrintable(result.error));
    QCOMPARE(*result.source, expected);
  }

  void replaceCodeBodyRejected_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QString>("error");
    QTest::newRow("non-code") << QByteArray("alpha\n") << QByteArray("new\n")
                              << QStringLiteral("fenced code target");
    QTest::newRow("nested") << QByteArray("> ```\n> old\n> ```\n")
                            << QByteArray("new\n")
                            << QStringLiteral("top-level");
    QTest::newRow("indented") << QByteArray("    old\n") << QByteArray("new\n")
                              << QStringLiteral("fenced code source map");
    QTest::newRow("invalid-utf8")
        << QByteArray("```\nold\n```\n") << QByteArray("\xFF", 1)
        << QStringLiteral("Invalid fenced code body");
    QTest::newRow("nul") << QByteArray("```\nold\n```\n")
                         << QByteArray("new\0old", 7)
                         << QStringLiteral("Invalid fenced code body");
    QTest::newRow("early-close") << QByteArray("```\nold\n```\n")
                                 << QByteArray("new\n```\n")
                                 << QStringLiteral("Structural fenced code");
  }

  void replaceCodeBodyRejected() {
    QFETCH(QByteArray, source);
    QFETCH(QByteArray, body);
    QFETCH(QString, error);
    const auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    auto code = find(doc, NodeKind::kBlock, MD_BLOCK_CODE);
    if (code >= doc.nodes.size()) {
      code = 0;
    }
    const auto result = editBytes(doc, ReplaceCodeBody{code, body});
    QVERIFY(!result.source);
    QVERIFY2(result.error.contains(error), qPrintable(result.error));
    QCOMPARE(doc.source, source);
    QCOMPARE(doc.revision, quint64(0));
  }

  void replaceCodeBodyNoop() {
    const QByteArray source = "```\nold\n```\n";
    const auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto code = find(doc, NodeKind::kBlock, MD_BLOCK_CODE);
    QVERIFY(code < doc.nodes.size());
    const auto result =
        applyEdit(doc, snapshotToken(doc), ReplaceCodeBody{code, "old\n"});
    QVERIFY2(result.document, qPrintable(result.error));
    QCOMPARE(result.document->source, source);
    QCOMPARE(result.document->revision, doc.revision);

    const auto foreign = parse("```\nother\n```\n");
    QVERIFY(foreign.document);
    const auto rejected = applyEdit(doc, snapshotToken(*foreign.document),
                                    ReplaceCodeBody{code, "new\n"});
    QVERIFY(!rejected.document);
    QVERIFY(!rejected.error.isEmpty());
  }

  void createdEmptyCodeSourceMap() {
    const QByteArray source = "```\n```\n";
    const auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto code = find(*parsed.document, NodeKind::kBlock, MD_BLOCK_CODE);
    QVERIFY(code < parsed.document->nodes.size());
    QString error;
    const auto mapped = mapFencedCodeBody(*parsed.document, code, error);
    QVERIFY2(mapped, qPrintable(error));
    QCOMPARE(mapped->body.size, qsizetype(0));
    QVERIFY(mapped->closed);
  }

  void createdTableSourceMap() {
    const QByteArray source =
        "| Column 1 | Column 2 |\n| --- | --- |\n|  |  |\n";
    const auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto table = find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
    QVERIFY(table < parsed.document->nodes.size());
    const auto mapped = mapTable(*parsed.document, table);
    QVERIFY2(mapped.table, qPrintable(mapped.error));
    QCOMPARE(mapped.table->columns, size_t(2));
    QCOMPARE(mapped.table->body.size(), size_t(1));
  }

  void insertTable_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<size_t>("position");
    QTest::addColumn<size_t>("columns");
    QTest::addColumn<size_t>("bodyRows");
    QTest::addColumn<QByteArray>("expected");
    QTest::newRow("empty") << QByteArray{} << size_t(0) << size_t(2)
                           << size_t(1)
                           << QByteArray("| Column 1 | Column 2 |\n"
                                         "| --- | --- |\n|  |  |\n");
    QTest::newRow("after") << QByteArray("alpha\n") << size_t(1) << size_t(2)
                           << size_t(1)
                           << QByteArray("alpha\n\n| Column 1 | Column 2 |\n"
                                         "| --- | --- |\n|  |  |\n");
    QTest::newRow("before") << QByteArray("beta\n") << size_t(0) << size_t(2)
                            << size_t(1)
                            << QByteArray("| Column 1 | Column 2 |\n"
                                          "| --- | --- |\n|  |  |\n\nbeta\n");
    QTest::newRow("middle") << QByteArray("a\n\nb\n") << size_t(1)
                            << size_t(2) << size_t(1)
                            << QByteArray("a\n\n| Column 1 | Column 2 |\n"
                                          "| --- | --- |\n|  |  |\n\nb\n");
    QTest::newRow("crlf") << QByteArray("alpha\r\n") << size_t(1)
                          << size_t(2) << size_t(1)
                          << QByteArray("alpha\r\n\r\n| Column 1 | Column 2 |"
                                        "\r\n| --- | --- |\r\n|  |  |\r\n");
    QTest::newRow("header-only") << QByteArray{} << size_t(0) << size_t(3)
                                 << size_t(0)
                                 << QByteArray("| Column 1 | Column 2 | "
                                               "Column 3 |\n| --- | --- | --- |\n");
  }

  void insertTable() {
    QFETCH(QByteArray, source);
    QFETCH(size_t, position);
    QFETCH(size_t, columns);
    QFETCH(size_t, bodyRows);
    QFETCH(QByteArray, expected);
    const auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto result =
        editBytes(*parsed.document,
                  InsertTable{position, columns, bodyRows});
    QVERIFY2(result.source, qPrintable(result.error));
    QCOMPARE(*result.source, expected);
  }

  void insertHorizontalRule_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<size_t>("position");
    QTest::addColumn<QByteArray>("expected");
    QTest::newRow("empty") << QByteArray{} << size_t(0)
                           << QByteArray("---\n");
    QTest::newRow("after") << QByteArray("alpha\n") << size_t(1)
                           << QByteArray("alpha\n\n---\n");
    QTest::newRow("before") << QByteArray("beta\n") << size_t(0)
                            << QByteArray("---\n\nbeta\n");
    QTest::newRow("crlf") << QByteArray("alpha\r\n") << size_t(1)
                          << QByteArray("alpha\r\n\r\n---\r\n");
  }

  void insertHorizontalRule() {
    QFETCH(QByteArray, source);
    QFETCH(size_t, position);
    QFETCH(QByteArray, expected);
    const auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto result =
        editBytes(*parsed.document, InsertHorizontalRule{position});
    QVERIFY2(result.source, qPrintable(result.error));
    QCOMPARE(*result.source, expected);
    const auto reparsed = parse(*result.source);
    QVERIFY(reparsed.document);
    QVERIFY(find(*reparsed.document, NodeKind::kBlock, MD_BLOCK_HR) <
            reparsed.document->nodes.size());
  }

  void insertFencedCode_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<size_t>("position");
    QTest::addColumn<QByteArray>("expected");
    QTest::newRow("empty") << QByteArray{} << size_t(0)
                           << QByteArray("```\n```\n");
    QTest::newRow("after") << QByteArray("alpha\n") << size_t(1)
                           << QByteArray("alpha\n\n```\n```\n");
    QTest::newRow("before") << QByteArray("beta\n") << size_t(0)
                            << QByteArray("```\n```\n\nbeta\n");
    QTest::newRow("crlf") << QByteArray("alpha\r\n") << size_t(1)
                          << QByteArray("alpha\r\n\r\n```\r\n```\r\n");
  }

  void insertFencedCode() {
    QFETCH(QByteArray, source);
    QFETCH(size_t, position);
    QFETCH(QByteArray, expected);
    const auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto result = editBytes(*parsed.document, InsertFencedCode{position});
    QVERIFY2(result.source, qPrintable(result.error));
    QCOMPARE(*result.source, expected);
    const auto reparsed = parse(*result.source);
    QVERIFY(reparsed.document);
    QVERIFY(find(*reparsed.document, NodeKind::kBlock, MD_BLOCK_CODE) <
            reparsed.document->nodes.size());
  }

  void insertBlockRejected() {
    const auto parsed = parse("alpha\n");
    QVERIFY(parsed.document);
    for (const auto& command :
         std::vector<EditCommand>{InsertHorizontalRule{2},
                                  InsertFencedCode{2}}) {
      const auto result = editBytes(*parsed.document, command);
      QVERIFY(!result.source);
      QVERIFY(result.error.contains("position"));
    }
    const auto foreign = parse("beta\n");
    QVERIFY(foreign.document);
    QVERIFY(!applyEdit(*parsed.document, snapshotToken(*foreign.document),
                       InsertHorizontalRule{1})
                 .document);
  }

  void insertTableRejected_data() {
    QTest::addColumn<size_t>("position");
    QTest::addColumn<size_t>("columns");
    QTest::addColumn<size_t>("bodyRows");
    QTest::addColumn<QString>("error");
    QTest::newRow("position") << size_t(2) << size_t(2) << size_t(1)
                              << QStringLiteral("position");
    QTest::newRow("columns") << size_t(0) << size_t(0) << size_t(1)
                             << QStringLiteral("dimensions");
    QTest::newRow("rows") << size_t(0) << size_t(2) << size_t(10001)
                          << QStringLiteral("dimensions");
  }

  void insertTableRejected() {
    QFETCH(size_t, position);
    QFETCH(size_t, columns);
    QFETCH(size_t, bodyRows);
    QFETCH(QString, error);
    const auto parsed = parse("alpha\n");
    QVERIFY(parsed.document);
    const auto result = editBytes(
        *parsed.document, InsertTable{position, columns, bodyRows});
    QVERIFY(!result.source);
    QVERIFY2(result.error.contains(error), qPrintable(result.error));
    QCOMPARE(parsed.document->source, QByteArray("alpha\n"));
    QCOMPARE(parsed.document->revision, quint64(0));
  }

  void insertTableStale() {
    const auto parsed = parse("alpha\n");
    QVERIFY(parsed.document);
    const auto foreign = parse("beta\n");
    QVERIFY(foreign.document);
    const auto result = applyEdit(*parsed.document,
                                  snapshotToken(*foreign.document),
                                  InsertTable{1, 2, 1});
    QVERIFY(!result.document);
    QVERIFY(!result.error.isEmpty());
  }

  void insertTableCellText_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<bool>("header");
    QTest::addColumn<QByteArray>("text");
    QTest::addColumn<QByteArray>("expected");
    QTest::newRow("body") << QByteArray("| h | i |\n| --- | --- |\n|  | b |\n")
                          << false << QByteArray("a")
                          << QByteArray("| h | i |\n| --- | --- |\n| a | b |\n");
    QTest::newRow("header")
        << QByteArray("|  | i |\n| --- | --- |\n| a | b |\n") << true
        << QByteArray("h")
        << QByteArray("| h | i |\n| --- | --- |\n| a | b |\n");
    QTest::newRow("pipe")
        << QByteArray("| h | i |\n| --- | --- |\n|  | b |\n") << false
        << QByteArray("a|b")
        << QByteArray("| h | i |\n| --- | --- |\n| a\\|b | b |\n");
    QTest::newRow("markup")
        << QByteArray("| h | i |\n| --- | --- |\n|  | b |\n") << false
        << QByteArray("*x*")
        << QByteArray("| h | i |\n| --- | --- |\n| \\*x\\* | b |\n");
    QTest::newRow("outside")
        << QByteArray("pre\n\n| h | i |\n| --- | --- |\n|  | b |\n\npost\n")
        << false << QByteArray("a")
        << QByteArray("pre\n\n| h | i |\n| --- | --- |\n| a | b |\n\npost\n");
    QTest::newRow("crlf")
        << QByteArray("| h | i |\r\n| --- | --- |\r\n|  | b |\r\n") << false
        << QByteArray("a")
        << QByteArray("| h | i |\r\n| --- | --- |\r\n| a | b |\r\n");
  }

  void insertTableCellText() {
    QFETCH(QByteArray, source);
    QFETCH(bool, header);
    QFETCH(QByteArray, text);
    QFETCH(QByteArray, expected);
    const auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto tableNode = find(doc, NodeKind::kBlock, MD_BLOCK_TABLE);
    const auto mapped = mapTable(doc, tableNode);
    QVERIFY2(mapped.table, qPrintable(mapped.error));
    const auto cell = header ? mapped.table->header.cells.front().node
                             : mapped.table->body.front().cells.front().node;
    const auto result = editBytes(doc, InsertTableCellText{cell, text});
    QVERIFY2(result.source, qPrintable(result.error));
    QCOMPARE(*result.source, expected);
  }

  void insertTableCellTextRejected_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<QByteArray>("text");
    QTest::addColumn<QString>("error");
    QTest::newRow("nonempty")
        << QByteArray("| h | i |\n| --- | --- |\n| a | b |\n")
        << QByteArray("x") << QStringLiteral("not empty");
    QTest::newRow("nested") << QByteArray("> | h |\n> | --- |\n> |  |\n")
                            << QByteArray("x") << QStringLiteral("top-level");
    QTest::newRow("empty") << QByteArray("| h |\n| --- |\n|  |\n")
                           << QByteArray("") << QStringLiteral("nonempty");
    QTest::newRow("newline") << QByteArray("| h |\n| --- |\n|  |\n")
                             << QByteArray("a\nb") << QStringLiteral("single-line");
    QTest::newRow("nul") << QByteArray("| h |\n| --- |\n|  |\n")
                         << QByteArray("a\0b", 3)
                         << QStringLiteral("single-line");
    QTest::newRow("invalid-utf8") << QByteArray("| h |\n| --- |\n|  |\n")
                                  << QByteArray("\xFF", 1)
                                  << QStringLiteral("UTF-8");
    QTest::newRow("whitespace") << QByteArray("| h |\n| --- |\n|  |\n")
                                << QByteArray(" a")
                                << QStringLiteral("single-line");
  }

  void insertTableCellTextRejected() {
    QFETCH(QByteArray, source);
    QFETCH(QByteArray, text);
    QFETCH(QString, error);
    const auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto tableNode = find(doc, NodeKind::kBlock, MD_BLOCK_TABLE);
    const auto mapped = mapTable(doc, tableNode);
    QVERIFY2(mapped.table, qPrintable(mapped.error));
    size_t cell = mapped.table->header.cells.front().node;
    if (!mapped.table->body.empty() && !mapped.table->body.front().cells.empty()) {
      cell = mapped.table->body.front().cells.front().node;
    }
    const auto result = editBytes(doc, InsertTableCellText{cell, text});
    QVERIFY(!result.source);
    QVERIFY2(result.error.contains(error), qPrintable(result.error));
    QCOMPARE(doc.source, source);
    QCOMPARE(doc.revision, quint64(0));
  }

  void insertTableCellTextStale() {
    const QByteArray source = "| h |\n| --- |\n|  |\n";
    const auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto mapped = mapTable(doc, find(doc, NodeKind::kBlock, MD_BLOCK_TABLE));
    QVERIFY(mapped.table);
    const auto cell = mapped.table->body.front().cells.front().node;
    const auto foreign = parse("| h |\n| --- |\n| x |\n");
    QVERIFY(foreign.document);
    const auto result = applyEdit(doc, snapshotToken(*foreign.document),
                                  InsertTableCellText{cell, "x"});
    QVERIFY(!result.document);
    QVERIFY(!result.error.isEmpty());
  }

  void wrapInListSecondBlock() {
    const QByteArray source = "before\n\nafter\n";
    const auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    QCOMPARE(doc.nodes[0].children.size(), size_t(2));
    const auto result = editBytes(doc, WrapInList{0, 1, false, 0});
    QVERIFY2(result.source, qPrintable(result.error));
    QCOMPARE(*result.source, QByteArray("before\n\n- after\n"));
    const auto reparsed = parse(*result.source);
    QVERIFY(reparsed.document);
    const auto& root = reparsed.document->nodes[0];
    QCOMPARE(root.children.size(), size_t(2));
    QCOMPARE(int(reparsed.document->nodes[root.children.front()].type),
             MD_BLOCK_P);
    QCOMPARE(int(reparsed.document->nodes[root.children.back()].type),
             MD_BLOCK_UL);
  }

  void wrapInListRejected_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<size_t>("parent");
    QTest::addColumn<size_t>("first");
    QTest::addColumn<bool>("ordered");
    QTest::addColumn<int>("start");
    QTest::addColumn<QString>("error");
    QTest::newRow("non-root") << QByteArray("alpha\n") << size_t(1) << size_t(0)
                              << false << 0 << QStringLiteral("root");
    QTest::newRow("position") << QByteArray("alpha\n") << size_t(0) << size_t(1)
                              << false << 0 << QStringLiteral("selection");
    QTest::newRow("heading") << QByteArray("# alpha\n") << size_t(0) << size_t(0)
                             << false << 0 << QStringLiteral("paragraph");
    QTest::newRow("list") << QByteArray("- alpha\n") << size_t(0) << size_t(0)
                          << false << 0 << QStringLiteral("paragraph");
    QTest::newRow("indented") << QByteArray("  alpha\n") << size_t(0)
                              << size_t(0) << false << 0
                              << QStringLiteral("unindented");
    QTest::newRow("continuation") << QByteArray("alpha\n  beta\n") << size_t(0)
                                  << size_t(0) << false << 0
                                  << QStringLiteral("unindented");
    QTest::newRow("ordered-start") << QByteArray("alpha\n") << size_t(0)
                                   << size_t(0) << true << 0
                                   << QStringLiteral("start");
  }

  void wrapInListRejected() {
    QFETCH(QByteArray, source);
    QFETCH(size_t, parent);
    QFETCH(size_t, first);
    QFETCH(bool, ordered);
    QFETCH(int, start);
    QFETCH(QString, error);
    const auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto result =
        editBytes(doc, WrapInList{parent, first, ordered, start});
    QVERIFY(!result.source);
    QVERIFY2(result.error.contains(error), qPrintable(result.error));
    QCOMPARE(doc.source, source);
    QCOMPARE(doc.revision, quint64(0));
  }

  void listRemove_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<int>("item");
    QTest::addColumn<QByteArray>("expected");
    const auto row = [](const char* name, const QByteArray& source, int item,
                        const QByteArray& expected) {
      QTest::newRow(name) << source << item << expected;
    };
    row("only", "- a\n", 0, "");
    row("first-tight", "- a\n- b\n", 0, "- b\n");
    row("last-tight", "- a\n- b\n", 1, "- a\n");
    row("first-loose", "- a\n\n- b\n", 0, "- b\n");
    row("last-loose", "- a\n\n- b\n", 1, "- a\n");
    row("ordered", "3. a\n9. b\n", 0, "3. b\n");
    row("unused-definition", "- [new]: /new\n\n  owner\n- keep\n", 0,
        "- keep\n");
    row("crlf", "- a\r\n- b\r\n", 1, "- a\r\n");
    row("multiline", "- a\n\n  ```txt\n  x\n  ```\n- b\n", 0, "- b\n");
  }

  void listRemove() {
    QFETCH(QByteArray, source);
    QFETCH(int, item);
    QFETCH(QByteArray, expected);
    const auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    auto list = find(doc, NodeKind::kBlock, MD_BLOCK_UL);
    if (list >= doc.nodes.size()) {
      list = find(doc, NodeKind::kBlock, MD_BLOCK_OL);
    }
    QVERIFY(list < doc.nodes.size());
    const auto mapped = mapList(doc, list);
    QVERIFY2(mapped.list, qPrintable(mapped.error));
    QVERIFY(size_t(item) < mapped.list->items.size());
    const auto target = mapped.list->items[size_t(item)].node;
    const auto result = editBytes(doc, RemoveListItem{target});
    QVERIFY2(result.source, qPrintable(result.error));
    QCOMPARE(*result.source, expected);
  }

  void listRemoveDefinitions() {
    const QByteArray inUse =
        "- [new]: /new \"N\"\n\n  owner\n- [sibling][new]\n";
    auto parsed = parse(inUse);
    QVERIFY(parsed.document);
    const auto item = find(*parsed.document, NodeKind::kBlock, MD_BLOCK_LI);
    auto rejected = editBytes(*parsed.document, RemoveListItem{item});
    QVERIFY(!rejected.source);
    QVERIFY2(rejected.error.contains("Definition in use"),
             qPrintable(rejected.error));

    const QByteArray fallback =
        "- [new]: /new\n\n  owner\n- [sibling][new]\n\n[new]: /new\n";
    auto fallbackParsed = parse(fallback);
    QVERIFY(fallbackParsed.document);
    const auto fallbackItem =
        find(*fallbackParsed.document, NodeKind::kBlock, MD_BLOCK_LI);
    const auto fallbackList =
        find(*fallbackParsed.document, NodeKind::kBlock, MD_BLOCK_UL);
    const auto fallbackMap = mapList(*fallbackParsed.document, fallbackList);
    QVERIFY2(fallbackMap.list, qPrintable(fallbackMap.error));
    auto fallbackResult =
        editBytes(*fallbackParsed.document, RemoveListItem{fallbackItem});
    QVERIFY(!fallbackResult.source);
    QVERIFY2(fallbackResult.error.contains("Definition in use"),
             qPrintable(fallbackResult.error));

    const QByteArray unused =
        "- [new]: /new\n\n  owner\n- keep\n\n[other]: /other\n";
    auto unusedParsed = parse(unused);
    QVERIFY(unusedParsed.document);
    const auto unusedItem =
        find(*unusedParsed.document, NodeKind::kBlock, MD_BLOCK_LI);
    auto unusedResult =
        editBytes(*unusedParsed.document, RemoveListItem{unusedItem});
    QVERIFY2(unusedResult.source, qPrintable(unusedResult.error));
    QCOMPARE(*unusedResult.source, QByteArray("- keep\n\n[other]: /other\n"));

    for (const QByteArray& use :
         {QByteArray("- [new][]\n"), QByteArray("- [new]\n"),
          QByteArray("- ![alt][new]\n"),
          QByteArray("| h |\n| --- |\n| [cell][new] |\n")}) {
      const QByteArray source =
          "- [new]: /new \"N\"\n\n  owner\n" +
          (use.startsWith('|') ? QByteArray("\n") : QByteArray()) + use;
      auto useParsed = parse(source);
      QVERIFY(useParsed.document);
      const auto useItem =
          find(*useParsed.document, NodeKind::kBlock, MD_BLOCK_LI);
      auto useResult =
          editBytes(*useParsed.document, RemoveListItem{useItem});
      QVERIFY2(!useResult.source &&
                   useResult.error.contains("Definition in use"),
               qPrintable(QString::fromUtf8(use) + QStringLiteral(" -> ") +
                          useResult.error));
    }

    const QByteArray unicode = QStringLiteral(
                                   "- [\u041c\u0435\u0442\u043a\u0430]: /x\n\n"
                                   "  owner\n- [s][\u041c\u0435\u0442\u043a\u0430]\n")
                                   .toUtf8();
    auto unicodeParsed = parse(unicode);
    QVERIFY(unicodeParsed.document);
    const auto unicodeItem =
        find(*unicodeParsed.document, NodeKind::kBlock, MD_BLOCK_LI);
    auto unicodeResult =
        editBytes(*unicodeParsed.document, RemoveListItem{unicodeItem});
    QVERIFY(!unicodeResult.source);
    QVERIFY(unicodeResult.error.contains("Definition in use"));
  }

  void listKind_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<bool>("ordered");
    QTest::addColumn<int>("start");
    QTest::addColumn<QByteArray>("expected");
    QTest::addColumn<bool>("success");
    QTest::newRow("to-ordered") << QByteArray("- a\n- b\n") << true << 1
                                << QByteArray("1. a\n2. b\n") << true;
    QTest::newRow("to-unordered") << QByteArray("1. a\n2. b\n") << false << 0
                                  << QByteArray("- a\n- b\n") << true;
    QTest::newRow("ordered-noop") << QByteArray("3. a\n9. b\n") << true << 3
                                  << QByteArray("3. a\n9. b\n") << true;
    QTest::newRow("unordered-noop") << QByteArray("- a\n- b\n") << false << 0
                                    << QByteArray("- a\n- b\n") << true;
    QTest::newRow("tasks") << QByteArray("- [ ] a\n- [x] b\n") << true << 1
                           << QByteArray("1. [ ] a\n2. [x] b\n") << true;
    QTest::newRow("definition")
        << QByteArray("- [new]: /new\n\n  [x][new]\n- b\n") << true << 1
        << QByteArray("1. [new]: /new\n\n   [x][new]\n2. b\n") << true;
    QTest::newRow("multiline") << QByteArray("- first\n  second\n") << true << 1
                               << QByteArray("1. first\n   second\n") << true;
    QTest::newRow("fenced")
        << QByteArray("- a\n\n  ```txt\n  x\n  ```\n") << true << 1
        << QByteArray("1. a\n\n   ```txt\n   x\n   ```\n") << true;
    QTest::newRow("crlf") << QByteArray("- a\r\n- b\r\n") << true << 1
                          << QByteArray("1. a\r\n2. b\r\n") << true;
    QTest::newRow("overflow") << QByteArray("1. a\n2. b\n") << true
                              << 999999999 << QByteArray() << false;
  }

  void listKind() {
    QFETCH(QByteArray, source);
    QFETCH(bool, ordered);
    QFETCH(int, start);
    QFETCH(QByteArray, expected);
    QFETCH(bool, success);
    const auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto list = find(doc, NodeKind::kBlock, MD_BLOCK_UL) <
                              doc.nodes.size()
                          ? find(doc, NodeKind::kBlock, MD_BLOCK_UL)
                          : find(doc, NodeKind::kBlock, MD_BLOCK_OL);
    QVERIFY(list < doc.nodes.size());
    const auto result = editBytes(doc, SetListKind{list, ordered, start});
    if (!success) {
      QVERIFY(!result.source);
      QVERIFY(result.error.contains("overflow"));
      return;
    }
    QVERIFY2(result.source, qPrintable(result.error));
    QCOMPARE(*result.source, expected);
    if (expected == source) {
      auto noop = applyEdit(doc, snapshotToken(doc), SetListKind{list, ordered, start});
      QVERIFY(noop.document);
      QCOMPARE(noop.document->revision, doc.revision);
      QCOMPARE(noop.document->identity, doc.identity);
    }
  }

  void listTask_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<int>("state");
    QTest::addColumn<QByteArray>("expected");
    QTest::addColumn<bool>("success");
    QTest::newRow("add-unchecked") << QByteArray("- a\n") << int(TaskState::kUnchecked)
                                   << QByteArray("- [ ] a\n") << true;
    QTest::newRow("add-checked") << QByteArray("- a\n") << int(TaskState::kChecked)
                                 << QByteArray("- [x] a\n") << true;
    QTest::newRow("remove") << QByteArray("- [ ] a\n") << int(TaskState::kNone)
                            << QByteArray("- a\n") << true;
    QTest::newRow("none-noop") << QByteArray("- a\n") << int(TaskState::kNone)
                               << QByteArray("- a\n") << true;
    QTest::newRow("checked-noop") << QByteArray("- [x] a\n")
                                  << int(TaskState::kChecked)
                                  << QByteArray("- [x] a\n") << true;
    QTest::newRow("ordered") << QByteArray("3. a\n9. b\n")
                             << int(TaskState::kUnchecked)
                             << QByteArray("3. [ ] a\n9. b\n") << true;
    QTest::newRow("external-definition")
        << QByteArray("[new]: /new\n\n- [sibling][new]\n")
        << int(TaskState::kUnchecked)
        << QByteArray("[new]: /new\n\n- [ ] [sibling][new]\n") << true;
    QTest::newRow("internal-definition")
        << QByteArray("- [new]: /new\n\n  owner\n- [sibling][new]\n")
        << int(TaskState::kUnchecked) << QByteArray() << false;
    QTest::newRow("crlf") << QByteArray("- a\r\n") << int(TaskState::kUnchecked)
                          << QByteArray("- [ ] a\r\n") << true;
    QTest::newRow("code-first") << QByteArray("- ```\n  x\n  ```\n")
                                << int(TaskState::kUnchecked) << QByteArray()
                                << false;
  }

  void listTask() {
    QFETCH(QByteArray, source);
    QFETCH(int, state);
    QFETCH(QByteArray, expected);
    QFETCH(bool, success);
    const auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto item = find(doc, NodeKind::kBlock, MD_BLOCK_LI);
    QVERIFY(item < doc.nodes.size());
    const auto result =
        editBytes(doc, SetItemTask{item, static_cast<TaskState>(state)});
    if (!success) {
      QVERIFY(!result.source);
      QVERIFY(!result.error.isEmpty());
      return;
    }
    QVERIFY2(result.source, qPrintable(result.error));
    QCOMPARE(*result.source, expected);
  }

  void listCommandGuards() {
    const auto parsed = parse("- a\n- b\n", 7);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto list = find(doc, NodeKind::kBlock, MD_BLOCK_UL);
    const auto item = find(doc, NodeKind::kBlock, MD_BLOCK_LI);
    const auto token = snapshotToken(doc);
    for (const auto& command :
         std::vector<EditCommand>{InsertListItem{list, 1, "c"},
                                  RemoveListItem{item},
                                  SetListKind{list, true, 1},
                                  SetItemTask{item, TaskState::kChecked}}) {
      const auto foreign = parse(doc.source, doc.revision);
      QVERIFY(foreign.document);
      QVERIFY(!applyEdit(doc, snapshotToken(*foreign.document), command).document);
      auto invalidDigest = token;
      invalidDigest.digest[0] = char(invalidDigest.digest[0] ^ 1);
      QVERIFY(!applyEdit(doc, invalidDigest, command).document);
    }
    QVERIFY(!applyEdit(doc, token, InsertListItem{doc.nodes.size(), 0, "x"})
                 .document);
    QVERIFY(!applyEdit(doc, token, InsertListItem{list, 3, "x"}).document);
    QVERIFY(!applyEdit(doc, token, RemoveListItem{doc.nodes.size()}).document);
    QVERIFY(!applyEdit(doc, token, SetListKind{doc.nodes.size(), true, 1})
                 .document);
    QVERIFY(!applyEdit(doc, token, SetItemTask{doc.nodes.size(), TaskState::kChecked})
                 .document);
    auto exhausted = parse(doc.source, std::numeric_limits<quint64>::max());
    QVERIFY(exhausted.document);
    auto overflow =
        applyEdit(*exhausted.document, snapshotToken(*exhausted.document),
                  InsertListItem{find(*exhausted.document, NodeKind::kBlock,
                                      MD_BLOCK_UL),
                                 1, "c"});
    QVERIFY(!overflow.document);
    QVERIFY(overflow.error.contains("overflow"));
  }

  void quoteSourceRanges() {
    const QByteArray crlf = "pre\r\n\r\n> a\r\n> b\r\n\r\npost\r\n";
    auto parsed = parse(crlf);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto quote = find(doc, NodeKind::kBlock, MD_BLOCK_QUOTE);
    QVERIFY(quote < doc.nodes.size());
    const auto mapped = mapQuote(doc, quote);
    QVERIFY2(mapped.quote, qPrintable(mapped.error));
    QCOMPARE(mapped.quote->range.offset, qsizetype(7));
    QCOMPARE(mapped.quote->range.size, qsizetype(10));
    QCOMPARE(doc.source.mid(mapped.quote->range.offset,
                            mapped.quote->range.size),
             QByteArray("> a\r\n> b\r\n"));

    const QByteArray multiline = "> a\n> b\n";
    auto multiParsed = parse(multiline);
    QVERIFY(multiParsed.document);
    const auto multiMap = mapQuote(
        *multiParsed.document,
        find(*multiParsed.document, NodeKind::kBlock, MD_BLOCK_QUOTE));
    QVERIFY2(multiMap.quote, qPrintable(multiMap.error));
    QCOMPARE(multiMap.quote->range.size, qsizetype(8));

    const QByteArray fenced = "> ```\n> x\n> ```\n";
    auto fencedParsed = parse(fenced);
    QVERIFY(fencedParsed.document);
    const auto fencedMap = mapQuote(
        *fencedParsed.document,
        find(*fencedParsed.document, NodeKind::kBlock, MD_BLOCK_QUOTE));
    QVERIFY2(fencedMap.quote, qPrintable(fencedMap.error));
    QCOMPARE(fencedMap.quote->range.offset, qsizetype(0));
    QCOMPARE(fencedMap.quote->range.size, fenced.size());

    const QByteArray html = "> <i>x</i>\n";
    auto htmlParsed = parse(html);
    QVERIFY(htmlParsed.document);
    const auto htmlMap = mapQuote(
        *htmlParsed.document,
        find(*htmlParsed.document, NodeKind::kBlock, MD_BLOCK_QUOTE));
    QVERIFY2(htmlMap.quote, qPrintable(htmlMap.error));

    const QByteArray lazy = "> a\nlazy\n";
    auto lazyParsed = parse(lazy);
    QVERIFY(lazyParsed.document);
    const auto lazyMap = mapQuote(
        *lazyParsed.document,
        find(*lazyParsed.document, NodeKind::kBlock, MD_BLOCK_QUOTE));
    QVERIFY(!lazyMap.quote);
    QVERIFY(lazyMap.error.startsWith("Unsupported"));

    const QByteArray duplicate = "> same\n\n> same\n";
    auto duplicateParsed = parse(duplicate);
    QVERIFY(duplicateParsed.document);
    std::vector<size_t> quotes;
    for (auto child : duplicateParsed.document->nodes[0].children) {
      if (duplicateParsed.document->nodes[child].type == MD_BLOCK_QUOTE) {
        quotes.push_back(child);
      }
    }
    QCOMPARE(quotes.size(), size_t(2));
    const auto firstMap =
        mapQuote(*duplicateParsed.document, quotes[0]);
    const auto secondMap =
        mapQuote(*duplicateParsed.document, quotes[1]);
    QVERIFY(firstMap.quote);
    QVERIFY(secondMap.quote);
    QCOMPARE(firstMap.quote->range.offset, qsizetype(0));
    QCOMPARE(secondMap.quote->range.offset, qsizetype(8));

    const QByteArray nested = "> > a\n";
    auto nestedParsed = parse(nested);
    QVERIFY(nestedParsed.document);
    const auto nestedMap = mapQuote(
        *nestedParsed.document,
        find(*nestedParsed.document, NodeKind::kBlock, MD_BLOCK_QUOTE));
    QVERIFY2(nestedMap.quote, qPrintable(nestedMap.error));
    QCOMPARE(nestedMap.quote->range.size, nested.size());

    const auto invalid = mapQuote(doc, doc.nodes.size());
    QVERIFY(!invalid.quote);
    QCOMPARE(invalid.error, QStringLiteral("Invalid quote target"));
  }

  void quoteWrap_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<int>("first");
    QTest::addColumn<int>("count");
    QTest::addColumn<QByteArray>("expected");
    QTest::addColumn<bool>("success");
    QTest::newRow("two-paragraphs") << QByteArray("a\n\nb\n") << 0 << 2
                                    << QByteArray("> a\n>\n> b\n") << true;
    QTest::newRow("one-paragraph") << QByteArray("a\n\n> b\n") << 0 << 1
                                   << QByteArray("> a\n\n> b\n") << true;
    QTest::newRow("fenced-code") << QByteArray("```\nx\n```\n") << 0 << 1
                                 << QByteArray("> ```\n> x\n> ```\n") << true;
    QTest::newRow("multiline-paragraph") << QByteArray("first\nsecond\n") << 0
                                         << 1
                                         << QByteArray("> first\n> second\n")
                                         << true;
    QTest::newRow("nested-quote") << QByteArray("> a\n") << 0 << 1
                                  << QByteArray("> > a\n") << true;
    QTest::newRow("list") << QByteArray("- a\n- b\n") << 0 << 1
                          << QByteArray("> - a\n> - b\n") << true;
    QTest::newRow("crlf") << QByteArray("a\r\n\r\nb\r\n") << 0 << 2
                          << QByteArray("> a\r\n>\r\n> b\r\n") << true;
    QTest::newRow("eof") << QByteArray("a") << 0 << 1 << QByteArray("> a")
                         << true;
    QTest::newRow("unicode")
        << QStringLiteral("\u0430\n\n\U0001f600\n").toUtf8() << 0 << 2
        << QStringLiteral("> \u0430\n>\n> \U0001f600\n").toUtf8() << true;
    QTest::newRow("merge-mismatch") << QByteArray("a\n> b\n") << 0 << 1
                                    << QByteArray() << false;
  }

  void quoteWrap() {
    QFETCH(QByteArray, source);
    QFETCH(int, first);
    QFETCH(int, count);
    QFETCH(QByteArray, expected);
    QFETCH(bool, success);
    const auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto result =
        applyEdit(doc, snapshotToken(doc),
                  WrapInQuote{0, size_t(first), size_t(count)});
    if (!success) {
      QVERIFY(!result.document);
      QVERIFY2(result.error.contains("semantics"), qPrintable(result.error));
      return;
    }
    QVERIFY2(result.document, qPrintable(result.error));
    QCOMPARE(result.document->source, expected);
    QCOMPARE(result.document->revision, doc.revision + 1);
    QCOMPARE(result.document->identity, doc.identity);
  }

  void quoteUnwrap_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<QByteArray>("expected");
    QTest::addColumn<bool>("success");
    QTest::newRow("crlf")
        << QByteArray("pre\r\n\r\n> a\r\n> b\r\n\r\npost\r\n")
        << QByteArray("pre\r\n\r\na\r\nb\r\n\r\npost\r\n") << true;
    QTest::newRow("two-paragraphs") << QByteArray("> a\n>\n> b\n")
                                    << QByteArray("a\n\nb\n") << true;
    QTest::newRow("nested") << QByteArray("> > a\n") << QByteArray("> a\n")
                            << true;
    QTest::newRow("eof") << QByteArray("> a") << QByteArray("a") << true;
    QTest::newRow("fenced") << QByteArray("> ```\n> x\n> ```\n")
                            << QByteArray("```\nx\n```\n") << true;
    QTest::newRow("list") << QByteArray("> - a\n> - b\n")
                          << QByteArray("- a\n- b\n") << true;
    QTest::newRow("inline-html") << QByteArray("> <i>x</i>\n")
                                 << QByteArray("<i>x</i>\n") << true;
    QTest::newRow("html-outside")
        << QByteArray("> a\n\n<!-- keep -->\n\n> b\n")
        << QByteArray("a\n\n<!-- keep -->\n\n> b\n") << true;
    QTest::newRow("unicode")
        << QStringLiteral("> \u0430\n> \U0001f600\n").toUtf8()
        << QStringLiteral("\u0430\n\U0001f600\n").toUtf8() << true;
    QTest::newRow("lazy") << QByteArray("> a\nlazy\n") << QByteArray()
                          << false;
  }

  void quoteUnwrap() {
    QFETCH(QByteArray, source);
    QFETCH(QByteArray, expected);
    QFETCH(bool, success);
    const auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto quote = find(doc, NodeKind::kBlock, MD_BLOCK_QUOTE);
    QVERIFY(quote < doc.nodes.size());
    const auto result =
        applyEdit(doc, snapshotToken(doc), UnwrapQuote{quote});
    if (!success) {
      QVERIFY(!result.document);
      QVERIFY(!result.error.isEmpty());
      return;
    }
    QVERIFY2(result.document, qPrintable(result.error));
    QCOMPARE(result.document->source, expected);
    QCOMPARE(result.document->revision, doc.revision + 1);
    QCOMPARE(result.document->identity, doc.identity);
  }

  void quoteGuards() {
    const auto parsed = parse("a\n\nb\n", 5);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto token = snapshotToken(doc);
    auto foreign = parse(doc.source, doc.revision);
    QVERIFY(foreign.document);
    for (const auto& command :
         std::vector<EditCommand>{WrapInQuote{0, 0, 2},
                                  UnwrapQuote{0}}) {
      QVERIFY(!applyEdit(doc, snapshotToken(*foreign.document), command)
                   .document);
      auto invalidDigest = token;
      invalidDigest.digest[0] = char(invalidDigest.digest[0] ^ 1);
      QVERIFY(!applyEdit(doc, invalidDigest, command).document);
    }
    for (const auto& command :
         std::vector<EditCommand>{WrapInQuote{0, 0, 0},
                                  WrapInQuote{0, 1, 2},
                                  WrapInQuote{0, 0, 3},
                                  WrapInQuote{1, 0, 1},
                                  WrapInQuote{find(doc, NodeKind::kText,
                                                   MD_TEXT_NORMAL, "a"),
                                              0, 1},
                                  UnwrapQuote{0},
                                  UnwrapQuote{doc.nodes.size()}}) {
      QVERIFY(!applyEdit(doc, token, command).document);
    }
    auto exhausted = parse(doc.source, std::numeric_limits<quint64>::max());
    QVERIFY(exhausted.document);
    auto overflow =
        applyEdit(*exhausted.document, snapshotToken(*exhausted.document),
                  WrapInQuote{0, 0, 2});
    QVERIFY(!overflow.document);
    QVERIFY(overflow.error.contains("overflow"));
  }

  void nestedContainerMapping() {
    const QByteArray nestedList = "- parent\n  - a\n  - b\n";
    auto parsed = parse(nestedList);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    size_t list = doc.nodes.size();
    for (size_t i = 0; i < doc.nodes.size(); ++i) {
      if (doc.nodes[i].type == MD_BLOCK_UL && doc.nodes[i].parent &&
          doc.nodes[*doc.nodes[i].parent].type == MD_BLOCK_LI) {
        list = i;
      }
    }
    QVERIFY(list < doc.nodes.size());
    const auto mapped = mapList(doc, list);
    QVERIFY2(mapped.list, qPrintable(mapped.error));
    QCOMPARE(mapped.list->range.offset, nestedList.indexOf("- a"));
    QCOMPARE(mapped.list->range.offset + mapped.list->range.size,
             qsizetype(nestedList.size()));
    QCOMPARE(mapped.list->items.size(), size_t(2));
    QCOMPARE(mapped.list->items[0].contentLines.size(), size_t(1));
    QCOMPARE(doc.source.mid(mapped.list->items[0].contentLines[0].offset,
                            mapped.list->items[0].contentLines[0].size),
             QByteArray("a\n"));
    QCOMPARE(mapped.list->continuationPrefix, QByteArray("  "));

    const QByteArray quotedList = "> - a\n> - b\n";
    auto quotedParsed = parse(quotedList);
    QVERIFY(quotedParsed.document);
    const auto& quotedDoc = *quotedParsed.document;
    size_t quoteList = quotedDoc.nodes.size();
    for (size_t i = 0; i < quotedDoc.nodes.size(); ++i) {
      if (quotedDoc.nodes[i].type == MD_BLOCK_UL) {
        quoteList = i;
        break;
      }
    }
    QVERIFY(quoteList < quotedDoc.nodes.size());
    const auto quoteListMap = mapList(quotedDoc, quoteList);
    QVERIFY2(quoteListMap.list, qPrintable(quoteListMap.error));
    QCOMPARE(quoteListMap.list->range.offset, quotedList.indexOf("- a"));
    QCOMPARE(quoteListMap.list->range.size, qsizetype(10));
    QCOMPARE(quoteListMap.list->continuationPrefix, QByteArray("> "));

    const QByteArray nestedQuote = "- parent\n\n  > a\n";
    auto quoteParsed = parse(nestedQuote);
    QVERIFY(quoteParsed.document);
    const auto& quoteDoc = *quoteParsed.document;
    size_t quote = quoteDoc.nodes.size();
    for (size_t i = 0; i < quoteDoc.nodes.size(); ++i) {
      if (quoteDoc.nodes[i].type == MD_BLOCK_QUOTE) {
        quote = i;
      }
    }
    QVERIFY(quote < quoteDoc.nodes.size());
    const auto quoteMap = mapQuote(quoteDoc, quote);
    QVERIFY2(quoteMap.quote, qPrintable(quoteMap.error));
    QCOMPARE(quoteMap.quote->range.offset, nestedQuote.indexOf("> a"));
    QCOMPARE(quoteMap.quote->continuationPrefix, QByteArray("  "));

    const QByteArray chain = "> - parent\n>\n>   > text\n";
    auto chainParsed = parse(chain);
    QVERIFY(chainParsed.document);
    const auto& chainDoc = *chainParsed.document;
    size_t innerQuote = chainDoc.nodes.size();
    for (size_t i = 0; i < chainDoc.nodes.size(); ++i) {
      if (chainDoc.nodes[i].type == MD_BLOCK_QUOTE && chainDoc.nodes[i].parent &&
          chainDoc.nodes[*chainDoc.nodes[i].parent].type == MD_BLOCK_LI) {
        innerQuote = i;
      }
    }
    QVERIFY(innerQuote < chainDoc.nodes.size());
    const auto chainMap = mapQuote(chainDoc, innerQuote);
    QVERIFY2(chainMap.quote, qPrintable(chainMap.error));
    QCOMPARE(chainMap.quote->range.offset, chain.indexOf("> text"));
    QCOMPARE(chainMap.quote->continuationPrefix, QByteArray(">   "));

    const QByteArray tightInline = "- parent **bold**\n  > quoted\n";
    auto tightParsed = parse(tightInline);
    QVERIFY(tightParsed.document);
    const auto& tightDoc = *tightParsed.document;
    size_t tightQuote = tightDoc.nodes.size();
    for (size_t i = 0; i < tightDoc.nodes.size(); ++i) {
      if (tightDoc.nodes[i].type == MD_BLOCK_QUOTE) {
        tightQuote = i;
      }
    }
    QVERIFY(tightQuote < tightDoc.nodes.size());
    const auto tightMap = mapQuote(tightDoc, tightQuote);
    QVERIFY2(tightMap.quote, qPrintable(tightMap.error));
    QCOMPARE(tightMap.quote->range.offset, tightInline.indexOf("> quoted"));

    for (const QByteArray& source :
         {QByteArray("> - a\nlazy\n"), QByteArray("> - a\n>\t- b\n"),
          QByteArray("> - ```\n>   open\n")}) {
      auto rejected = parse(source);
      QVERIFY(rejected.document);
      bool found = false;
      for (size_t i = 0; i < rejected.document->nodes.size(); ++i) {
        if (rejected.document->nodes[i].type == MD_BLOCK_UL) {
          const auto result = mapList(*rejected.document, i);
          QVERIFY(!result.list);
          found = true;
        }
      }
      QVERIFY(found);
    }
  }

  void nestedListCommands() {
    const auto findListWithParent = [](const Document& document,
                                       int parentType) -> size_t {
      for (size_t i = 0; i < document.nodes.size(); ++i) {
        const auto& node = document.nodes[i];
        if (node.kind == NodeKind::kBlock &&
            (node.type == MD_BLOCK_UL || node.type == MD_BLOCK_OL) &&
            node.parent &&
            document.nodes[*node.parent].type == parentType) {
          return i;
        }
      }
      return document.nodes.size();
    };

    {
      const QByteArray source = "- parent\n  - a\n  - b\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto list = findListWithParent(*parsed.document, MD_BLOCK_LI);
      QVERIFY(list < parsed.document->nodes.size());
      const auto result =
          editBytes(*parsed.document, InsertListItem{list, 1, "c"});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("- parent\n  - a\n  - c\n  - b\n"));
    }
    {
      const QByteArray source = "> - a\n> - b\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto list = findListWithParent(*parsed.document, MD_BLOCK_QUOTE);
      QVERIFY(list < parsed.document->nodes.size());
      const auto mapped = mapList(*parsed.document, list);
      QVERIFY(mapped.list);
      const auto target = mapped.list->items[1].node;
      const auto result =
          editBytes(*parsed.document, RemoveListItem{target});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("> - a\n"));
    }
    {
      const QByteArray source = "- parent\n\n  > a\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      size_t quote = parsed.document->nodes.size();
      for (size_t i = 0; i < parsed.document->nodes.size(); ++i) {
        if (parsed.document->nodes[i].type == MD_BLOCK_QUOTE) {
          quote = i;
        }
      }
      QVERIFY(quote < parsed.document->nodes.size());
      const auto result =
          editBytes(*parsed.document, UnwrapQuote{quote});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("- parent\n\n  a\n"));
    }
    {
      const QByteArray source = "- parent\n\n  second\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto text =
          find(*parsed.document, NodeKind::kText, MD_TEXT_NORMAL, "second");
      QVERIFY(text < parsed.document->nodes.size());
      QVERIFY(parsed.document->nodes[text].parent);
      const auto paragraph = *parsed.document->nodes[text].parent;
      QVERIFY(parsed.document->nodes[paragraph].parent);
      const auto parent = *parsed.document->nodes[paragraph].parent;
      const auto& siblings = parsed.document->nodes[parent].children;
      const auto position =
          std::find(siblings.begin(), siblings.end(), paragraph) -
          siblings.begin();
      const auto result = editBytes(
          *parsed.document,
          WrapInQuote{parent, size_t(position), 1});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("- parent\n\n  > second\n"));
    }
    {
      const QByteArray source = "> - parent\n>\n>   > text\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      size_t inner = parsed.document->nodes.size();
      for (size_t i = 0; i < parsed.document->nodes.size(); ++i) {
        if (parsed.document->nodes[i].type == MD_BLOCK_QUOTE &&
            parsed.document->nodes[i].parent &&
            parsed.document->nodes[*parsed.document->nodes[i].parent].type ==
                MD_BLOCK_LI) {
          inner = i;
        }
      }
      QVERIFY(inner < parsed.document->nodes.size());
      const auto result =
          editBytes(*parsed.document, UnwrapQuote{inner});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("> - parent\n>\n>   text\n"));
    }
    {
      const QByteArray source = "- parent\n  1. a\n  2. b\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto list = findListWithParent(*parsed.document, MD_BLOCK_LI);
      QVERIFY(list < parsed.document->nodes.size());
      const auto mapped = mapList(*parsed.document, list);
      QVERIFY(mapped.list);
      const auto target = mapped.list->items[0].node;
      const auto result = editBytes(
          *parsed.document, SetItemTask{target, TaskState::kUnchecked});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("- parent\n  1. [ ] a\n  2. b\n"));
    }
    {
      const QByteArray source = "- parent\n  1. a\n  2. b\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto list = findListWithParent(*parsed.document, MD_BLOCK_LI);
      QVERIFY(list < parsed.document->nodes.size());
      const auto result =
          editBytes(*parsed.document, SetListKind{list, false, 0});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("- parent\n  - a\n  - b\n"));
    }
    {
      const QByteArray source = "> - a\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto list = findListWithParent(*parsed.document, MD_BLOCK_QUOTE);
      QVERIFY(list < parsed.document->nodes.size());
      const auto mapped = mapList(*parsed.document, list);
      QVERIFY(mapped.list);
      const auto result =
          editBytes(*parsed.document, RemoveListItem{mapped.list->items[0].node});
      QVERIFY(!result.source);
      QVERIFY2(result.error.contains("cascade"), qPrintable(result.error));
    }
    {
      const QByteArray source = "- parent\n\n  3. a\n  9. b\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto list = findListWithParent(*parsed.document, MD_BLOCK_LI);
      QVERIFY(list < parsed.document->nodes.size());
      const auto result =
          editBytes(*parsed.document, InsertListItem{list, 1, "c"});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("- parent\n\n  3. a\n  4. c\n  5. b\n"));
    }
    {
      const QByteArray source = "- parent\r\n  - a\r\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto list = findListWithParent(*parsed.document, MD_BLOCK_LI);
      QVERIFY(list < parsed.document->nodes.size());
      const auto result =
          editBytes(*parsed.document, InsertListItem{list, 1, "b"});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("- parent\r\n  - a\r\n  - b\r\n"));
    }
    {
      const QByteArray source = "- parent\n  - a";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto list = findListWithParent(*parsed.document, MD_BLOCK_LI);
      QVERIFY(list < parsed.document->nodes.size());
      const auto result =
          editBytes(*parsed.document, InsertListItem{list, 1, "b"});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("- parent\n  - a\n  - b"));
    }
    {
      const auto cyrillic = QStringLiteral("\u0430").toUtf8();
      const auto emoji = QStringLiteral("\U0001f600").toUtf8();
      const QByteArray source = "- parent\n  - " + cyrillic + "\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto list = findListWithParent(*parsed.document, MD_BLOCK_LI);
      QVERIFY(list < parsed.document->nodes.size());
      const auto result =
          editBytes(*parsed.document, InsertListItem{list, 1, emoji});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               "- parent\n  - " + cyrillic + "\n  - " + emoji + "\n");
    }
    {
      const QByteArray source = "- parent\n\n  - a\n\n  - b\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto list = findListWithParent(*parsed.document, MD_BLOCK_LI);
      QVERIFY(list < parsed.document->nodes.size());
      const auto result =
          editBytes(*parsed.document, InsertListItem{list, 2, "c"});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("- parent\n\n  - a\n\n  - b\n\n  - c\n"));
    }
    {
      const QByteArray source =
          "- one\n  - same\n- two\n  - same\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      std::vector<size_t> lists;
      for (size_t i = 0; i < parsed.document->nodes.size(); ++i) {
        if (parsed.document->nodes[i].type == MD_BLOCK_UL &&
            parsed.document->nodes[i].parent &&
            parsed.document->nodes[*parsed.document->nodes[i].parent].type ==
                MD_BLOCK_LI) {
          lists.push_back(i);
        }
      }
      QCOMPARE(lists.size(), size_t(2));
      const auto result =
          editBytes(*parsed.document, InsertListItem{lists[1], 1, "new"});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("- one\n  - same\n- two\n  - same\n  - new\n"));
    }
  }

  void nestedQuoteCommands() {
    {
      const QByteArray source = "> a\n> b\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto& doc = *parsed.document;
      size_t quote = doc.nodes.size();
      for (size_t i = 0; i < doc.nodes.size(); ++i) {
        if (doc.nodes[i].type == MD_BLOCK_QUOTE) {
          quote = i;
          break;
        }
      }
      QVERIFY(quote < doc.nodes.size());
      QVERIFY(!doc.nodes[quote].children.empty());
      const auto paragraph = doc.nodes[quote].children.front();
      const auto result =
          editBytes(doc, WrapInQuote{quote, 0, 1});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("> > a\n> > b\n"));
      Q_UNUSED(paragraph);
    }
    {
      const QByteArray source = "> outer\n>\n> > inner\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto& doc = *parsed.document;
      size_t inner = doc.nodes.size();
      for (size_t i = 0; i < doc.nodes.size(); ++i) {
        if (doc.nodes[i].type == MD_BLOCK_QUOTE && doc.nodes[i].parent &&
            doc.nodes[*doc.nodes[i].parent].type == MD_BLOCK_QUOTE) {
          inner = i;
        }
      }
      QVERIFY(inner < doc.nodes.size());
      const auto result = editBytes(doc, UnwrapQuote{inner});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("> outer\n>\n> inner\n"));
    }
    {
      const QByteArray source = "- parent\n\n  one\n\n  two\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto& doc = *parsed.document;
      size_t li = doc.nodes.size();
      for (size_t i = 0; i < doc.nodes.size(); ++i) {
        if (doc.nodes[i].type == MD_BLOCK_LI) {
          li = i;
        }
      }
      QVERIFY(li < doc.nodes.size());
      const auto result = editBytes(doc, WrapInQuote{li, 1, 2});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("- parent\n\n  > one\n  >\n  > two\n"));
      auto wrapped = parse(*result.source);
      QVERIFY(wrapped.document);
      size_t quote = wrapped.document->nodes.size();
      for (size_t i = 0; i < wrapped.document->nodes.size(); ++i) {
        if (wrapped.document->nodes[i].type == MD_BLOCK_QUOTE) {
          quote = i;
        }
      }
      QVERIFY(quote < wrapped.document->nodes.size());
      const auto unwrapped =
          editBytes(*wrapped.document, UnwrapQuote{quote});
      QVERIFY2(unwrapped.source, qPrintable(unwrapped.error));
      QCOMPARE(*unwrapped.source, source);
    }
  }

  void nestedListReferences() {
    {
      const QByteArray source = "[outer]: /keep \"K\"\n\n> - a\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      size_t list = parsed.document->nodes.size();
      for (size_t i = 0; i < parsed.document->nodes.size(); ++i) {
        if (parsed.document->nodes[i].type == MD_BLOCK_UL) {
          list = i;
        }
      }
      QVERIFY(list < parsed.document->nodes.size());
      const auto result =
          editBytes(*parsed.document, InsertListItem{list, 1, "[x][outer]"});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("[outer]: /keep \"K\"\n\n> - a\n> - [x][outer]\n"));
      auto reparsed = parse(*result.source);
      QVERIFY(reparsed.document);
      const auto link = find(*reparsed.document, NodeKind::kSpan, MD_SPAN_A);
      QVERIFY(link < reparsed.document->nodes.size());
      QCOMPARE(reparsed.document->nodes[link].attributes["href"].toString(),
               QStringLiteral("/keep"));
    }
    {
      const QByteArray source = "> - a\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto list = find(*parsed.document, NodeKind::kBlock, MD_BLOCK_UL);
      QVERIFY(list < parsed.document->nodes.size());
      const auto result = editBytes(
          *parsed.document,
          InsertListItem{list, 1, "[new]: /new \"N\"\n\n[x][new]"});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("> - a\n> - [new]: /new \"N\"\n>\n>   [x][new]\n"));
      auto reparsed = parse(*result.source);
      QVERIFY(reparsed.document);
      const auto link = find(*reparsed.document, NodeKind::kSpan, MD_SPAN_A);
      QVERIFY(link < reparsed.document->nodes.size());
      QCOMPARE(reparsed.document->nodes[link].attributes["href"].toString(),
               QStringLiteral("/new"));
      QCOMPARE(reparsed.document->nodes[link].attributes["title"].toString(),
               QStringLiteral("N"));
    }
    {
      const QByteArray source =
          "> - [new]: /new \"N\"\n>\n>   owner\n> - [s][new]\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto list = find(*parsed.document, NodeKind::kBlock, MD_BLOCK_UL);
      QVERIFY(list < parsed.document->nodes.size());
      const auto mapped = mapList(*parsed.document, list);
      QVERIFY(mapped.list);
      const auto result =
          editBytes(*parsed.document, RemoveListItem{mapped.list->items[0].node});
      QVERIFY(!result.source);
      QVERIFY2(result.error.contains("Definition in use"),
               qPrintable(result.error));
    }
    {
      const QByteArray source = "[new]: /old\n\n> - a\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto list = find(*parsed.document, NodeKind::kBlock, MD_BLOCK_UL);
      QVERIFY(list < parsed.document->nodes.size());
      const auto result = editBytes(
          *parsed.document,
          InsertListItem{list, 1, "[new]: /new\n\n[x][new]"});
      QVERIFY(!result.source);
      QVERIFY2(result.error.contains("Definition conflict"),
               qPrintable(result.error));
    }
    {
      const QByteArray source =
          "> - [new]: /old\n>\n>   owner\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto list = find(*parsed.document, NodeKind::kBlock, MD_BLOCK_UL);
      QVERIFY(list < parsed.document->nodes.size());
      const auto result = editBytes(
          *parsed.document,
          InsertListItem{list, 1, "[new]: /new\n\n[x][new]"});
      QVERIFY(!result.source);
      QVERIFY2(result.error.contains("Definition conflict"),
               qPrintable(result.error));
    }
    {
      const QByteArray source = "[x][new]\n\n> - a\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto list = find(*parsed.document, NodeKind::kBlock, MD_BLOCK_UL);
      QVERIFY(list < parsed.document->nodes.size());
      const auto result = editBytes(
          *parsed.document,
          InsertListItem{list, 1, "[new]: /new\n\n[x][new]"});
      QVERIFY(!result.source);
      QVERIFY2(result.error.contains("Binding conflict"),
               qPrintable(result.error));
    }
  }

  void indentOutdentCommands() {
    const auto outerItems = [](const Document& document, int type) {
      const auto list = find(document, NodeKind::kBlock, type);
      const auto mapped = mapList(document, list);
      return mapped;
    };

    {
      auto parsed = parse("- a\n- b\n");
      QVERIFY(parsed.document);
      const auto mapped = outerItems(*parsed.document, MD_BLOCK_UL);
      QVERIFY(mapped.list);
      const auto result = applyEdit(
          *parsed.document, snapshotToken(*parsed.document),
          IndentListItem{mapped.list->items[1].node});
      QVERIFY2(result.document, qPrintable(result.error));
      QCOMPARE(result.document->source, QByteArray("- a\n  - b\n"));
      QCOMPARE(result.document->revision, parsed.document->revision + 1);
      QCOMPARE(result.document->identity, parsed.document->identity);
    }
    {
      const QByteArray source = "- a\n  - b\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto outer = find(*parsed.document, NodeKind::kBlock, MD_BLOCK_UL);
      const auto outerMap = mapList(*parsed.document, outer);
      QVERIFY(outerMap.list);
      size_t nestedList = parsed.document->nodes.size();
      for (size_t i = 0; i < parsed.document->nodes.size(); ++i) {
        if (parsed.document->nodes[i].type == MD_BLOCK_UL &&
            parsed.document->nodes[i].parent &&
            parsed.document->nodes[*parsed.document->nodes[i].parent].type ==
                MD_BLOCK_LI) {
          nestedList = i;
        }
      }
      QVERIFY(nestedList < parsed.document->nodes.size());
      const auto nestedMap = mapList(*parsed.document, nestedList);
      QVERIFY(nestedMap.list);
      const auto result =
          editBytes(*parsed.document,
                    OutdentListItem{nestedMap.list->items[0].node});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("- a\n- b\n"));
    }
    {
      auto parsed = parse("- a\n  - x\n- b\n");
      QVERIFY(parsed.document);
      const auto mapped = outerItems(*parsed.document, MD_BLOCK_UL);
      QVERIFY(mapped.list);
      const auto result = editBytes(
          *parsed.document, IndentListItem{mapped.list->items[1].node});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("- a\n  - x\n  - b\n"));
    }
    {
      auto parsed = parse("- a\n  3. x\n- b\n");
      QVERIFY(parsed.document);
      const auto mapped = outerItems(*parsed.document, MD_BLOCK_UL);
      QVERIFY(mapped.list);
      const auto result = editBytes(
          *parsed.document, IndentListItem{mapped.list->items[1].node});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("- a\n  3. x\n  - b\n"));
    }
    {
      const QByteArray source = "- a\n  - x\n  - y\n  - z\n- b\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      size_t nestedList = parsed.document->nodes.size();
      for (size_t i = 0; i < parsed.document->nodes.size(); ++i) {
        if (parsed.document->nodes[i].type == MD_BLOCK_UL &&
            parsed.document->nodes[i].parent &&
            parsed.document->nodes[*parsed.document->nodes[i].parent].type ==
                MD_BLOCK_LI) {
          nestedList = i;
        }
      }
      QVERIFY(nestedList < parsed.document->nodes.size());
      const auto nested = mapList(*parsed.document, nestedList);
      QVERIFY(nested.list);
      const auto result = editBytes(
          *parsed.document, OutdentListItem{nested.list->items[1].node});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("- a\n  - x\n  - z\n- y\n- b\n"));
    }
    {
      auto parsed = parse("- a\n- b\n");
      QVERIFY(parsed.document);
      const auto mapped = outerItems(*parsed.document, MD_BLOCK_UL);
      QVERIFY(mapped.list);
      const auto result = editBytes(
          *parsed.document, IndentListItem{mapped.list->items[0].node});
      QVERIFY(!result.source);
      QVERIFY2(result.error.contains("previous"),
               qPrintable(result.error));
    }
    {
      auto parsed = parse("- a\n");
      QVERIFY(parsed.document);
      const auto mapped = outerItems(*parsed.document, MD_BLOCK_UL);
      QVERIFY(mapped.list);
      const auto result = editBytes(
          *parsed.document, OutdentListItem{mapped.list->items[0].node});
      QVERIFY(!result.source);
      QVERIFY2(result.error.contains("top-level"),
               qPrintable(result.error));
    }
    {
      auto parsed = parse("- a\r\n- b\r\n");
      QVERIFY(parsed.document);
      const auto mapped = outerItems(*parsed.document, MD_BLOCK_UL);
      QVERIFY(mapped.list);
      const auto result = editBytes(
          *parsed.document, IndentListItem{mapped.list->items[1].node});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("- a\r\n  - b\r\n"));
    }
    {
      auto parsed = parse("- a\n- b");
      QVERIFY(parsed.document);
      const auto mapped = outerItems(*parsed.document, MD_BLOCK_UL);
      QVERIFY(mapped.list);
      const auto result = editBytes(
          *parsed.document, IndentListItem{mapped.list->items[1].node});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("- a\n  - b"));
    }
    {
      auto parsed = parse("- [ ] a\n- [x] b\n");
      QVERIFY(parsed.document);
      const auto mapped = outerItems(*parsed.document, MD_BLOCK_UL);
      QVERIFY(mapped.list);
      const auto result = editBytes(
          *parsed.document, IndentListItem{mapped.list->items[1].node});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("- [ ] a\n  - [x] b\n"));
    }
    {
      auto parsed = parse("> - a\n> - b\n");
      QVERIFY(parsed.document);
      const auto list = find(*parsed.document, NodeKind::kBlock, MD_BLOCK_UL);
      const auto mapped = mapList(*parsed.document, list);
      QVERIFY(mapped.list);
      const auto result = editBytes(
          *parsed.document, IndentListItem{mapped.list->items[1].node});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("> - a\n>   - b\n"));
    }
    {
      auto parsed = parse("> - a\n>   - b\n");
      QVERIFY(parsed.document);
      size_t nestedList = parsed.document->nodes.size();
      for (size_t i = 0; i < parsed.document->nodes.size(); ++i) {
        if (parsed.document->nodes[i].type == MD_BLOCK_UL &&
            parsed.document->nodes[i].parent &&
            parsed.document->nodes[*parsed.document->nodes[i].parent].type ==
                MD_BLOCK_LI) {
          nestedList = i;
        }
      }
      QVERIFY(nestedList < parsed.document->nodes.size());
      const auto nested = mapList(*parsed.document, nestedList);
      QVERIFY(nested.list);
      const auto result = editBytes(
          *parsed.document, OutdentListItem{nested.list->items[0].node});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("> - a\n> - b\n"));
    }
    {
      auto parsed =
          parse("- a\n- b\n\n  ```txt\n  x\n  ```\n");
      QVERIFY(parsed.document);
      const auto mapped = outerItems(*parsed.document, MD_BLOCK_UL);
      QVERIFY(mapped.list);
      const auto result = editBytes(
          *parsed.document, IndentListItem{mapped.list->items[1].node});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("- a\n  - b\n\n    ```txt\n    x\n    ```\n"));
    }
    {
      auto parsed = parse("1. a\n\n   999999999. x\n2. b\n");
      QVERIFY(parsed.document);
      const auto outer = find(*parsed.document, NodeKind::kBlock, MD_BLOCK_OL);
      const auto mapped = mapList(*parsed.document, outer);
      QVERIFY(mapped.list);
      const auto result = editBytes(
          *parsed.document, IndentListItem{mapped.list->items[1].node});
      QVERIFY(!result.source);
      QVERIFY2(result.error.contains("overflow"), qPrintable(result.error));
    }
  }

  void tableSourceRanges() {
    const QByteArray source = "pre\n\n| h |\n| --- |\n| a |\n\npost\n";
    auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto table = find(doc, NodeKind::kBlock, MD_BLOCK_TABLE);
    QVERIFY(table < doc.nodes.size());
    const auto mapped = mapTable(doc, table);
    QVERIFY2(mapped.table, qPrintable(mapped.error));
    QCOMPARE(mapped.table->range.offset, qsizetype(5));
    QCOMPARE(mapped.table->range.size, qsizetype(20));
    QCOMPARE(mapped.table->columns, size_t(1));
    QCOMPARE(mapped.table->header.range.offset, qsizetype(5));
    QCOMPARE(mapped.table->header.range.size, qsizetype(6));
    QCOMPARE(mapped.table->delimiterRange.offset, qsizetype(11));
    QCOMPARE(mapped.table->delimiterRange.size, qsizetype(8));
    QCOMPARE(mapped.table->body.size(), size_t(1));
    QCOMPARE(mapped.table->body[0].range.offset, qsizetype(19));
    QCOMPARE(mapped.table->body[0].range.size, qsizetype(6));
    QCOMPARE(mapped.table->header.cells.size(), size_t(1));
    QCOMPARE(mapped.table->header.cells[0].range.offset, qsizetype(7));
    QCOMPARE(mapped.table->header.cells[0].range.size, qsizetype(1));
    QCOMPARE(mapped.table->body[0].cells[0].range.offset, qsizetype(21));
    QCOMPARE(mapped.table->body[0].cells[0].range.size, qsizetype(1));
    QCOMPARE(doc.source.mid(mapped.table->header.cells[0].range.offset,
                            mapped.table->header.cells[0].range.size),
             QByteArray("h"));

    const QByteArray headerOnly = "| h |\n| --- |\n";
    auto headerParsed = parse(headerOnly);
    QVERIFY(headerParsed.document);
    const auto headerMap =
        mapTable(*headerParsed.document,
                 find(*headerParsed.document, NodeKind::kBlock, MD_BLOCK_TABLE));
    QVERIFY2(headerMap.table, qPrintable(headerMap.error));
    QCOMPARE(headerMap.table->columns, size_t(1));
    QCOMPARE(headerMap.table->body.size(), size_t(0));
    QCOMPARE(headerMap.table->range.size, qsizetype(headerOnly.size()));
    QCOMPARE(headerMap.table->delimiterCells.size(), size_t(1));
    QCOMPARE(headerMap.table->delimiterCells[0].offset, qsizetype(8));
    QCOMPARE(headerMap.table->delimiterCells[0].size, qsizetype(3));

    const QByteArray noOuter = "a | b\n--- | ---\nx | y\n";
    auto noOuterParsed = parse(noOuter);
    QVERIFY(noOuterParsed.document);
    const auto noOuterMap =
        mapTable(*noOuterParsed.document,
                 find(*noOuterParsed.document, NodeKind::kBlock, MD_BLOCK_TABLE));
    QVERIFY2(noOuterMap.table, qPrintable(noOuterMap.error));
    QCOMPARE(noOuterMap.table->columns, size_t(2));
    QCOMPARE(noOuterMap.table->body.size(), size_t(1));
    QCOMPARE(noOuterMap.table->header.cells.size(), size_t(2));
    QCOMPARE(noOuterMap.table->body[0].cells.size(), size_t(2));
    QCOMPARE(noOuterParsed.document->source.mid(
                 noOuterMap.table->header.cells[0].range.offset,
                 noOuterMap.table->header.cells[0].range.size),
             QByteArray("a"));
    QCOMPARE(noOuterParsed.document->source.mid(
                 noOuterMap.table->body[0].cells[1].range.offset,
                 noOuterMap.table->body[0].cells[1].range.size),
             QByteArray("y"));

    const QByteArray code = "| h |\n| --- |\n| `a\\|b` |\n";
    auto codeParsed = parse(code);
    QVERIFY(codeParsed.document);
    const auto codeMap =
        mapTable(*codeParsed.document,
                 find(*codeParsed.document, NodeKind::kBlock, MD_BLOCK_TABLE));
    QVERIFY2(codeMap.table, qPrintable(codeMap.error));
    QCOMPARE(codeMap.table->body[0].cells.size(), size_t(1));
    QCOMPARE(codeParsed.document->source.mid(
                 codeMap.table->body[0].cells[0].range.offset,
                 codeMap.table->body[0].cells[0].range.size),
             QByteArray("`a\\|b`"));

    const QByteArray inlineFragment =
        "| h |\n| --- |\n| *x* [y](/url) &amp; <i>z</i> |\n";
    auto inlineParsed = parse(inlineFragment);
    QVERIFY(inlineParsed.document);
    const auto inlineMap =
        mapTable(*inlineParsed.document,
                 find(*inlineParsed.document, NodeKind::kBlock, MD_BLOCK_TABLE));
    QVERIFY2(inlineMap.table, qPrintable(inlineMap.error));
    QCOMPARE(inlineParsed.document->source.mid(
                 inlineMap.table->body[0].cells[0].range.offset,
                 inlineMap.table->body[0].cells[0].range.size),
             QByteArray("*x* [y](/url) &amp; <i>z</i>"));

    const QByteArray ragged =
        "| h | i |\n| --- | --- |\n| a |\n";
    auto raggedParsed = parse(ragged);
    QVERIFY(raggedParsed.document);
    const auto raggedMap =
        mapTable(*raggedParsed.document,
                 find(*raggedParsed.document, NodeKind::kBlock, MD_BLOCK_TABLE));
    QVERIFY(!raggedMap.table);
    QVERIFY(raggedMap.error.contains("ragged"));

    const QByteArray reference =
        "[new]: /new \"N\"\n\n| h |\n| --- |\n| [cell][new] |\n";
    auto referenceParsed = parse(reference);
    QVERIFY(referenceParsed.document);
    const auto referenceMap =
        mapTable(*referenceParsed.document,
                 find(*referenceParsed.document, NodeKind::kBlock, MD_BLOCK_TABLE));
    QVERIFY2(referenceMap.table, qPrintable(referenceMap.error));
    QCOMPARE(referenceMap.table->range.offset, qsizetype(17));
    QCOMPARE(referenceMap.table->range.size, qsizetype(30));
    QCOMPARE(referenceParsed.document->source.mid(
                 referenceMap.table->body[0].cells[0].range.offset,
                 referenceMap.table->body[0].cells[0].range.size),
             QByteArray("[cell][new]"));
    const auto link =
        find(*referenceParsed.document, NodeKind::kSpan, MD_SPAN_A);
    QVERIFY(link < referenceParsed.document->nodes.size());
    QCOMPARE(referenceParsed.document->nodes[link].attributes["href"].toString(),
             QStringLiteral("/new"));

    const QByteArray aligned = "| h |\n| :---: |\n";
    auto alignedParsed = parse(aligned);
    QVERIFY(alignedParsed.document);
    const auto alignedMap =
        mapTable(*alignedParsed.document,
                 find(*alignedParsed.document, NodeKind::kBlock, MD_BLOCK_TABLE));
    QVERIFY2(alignedMap.table, qPrintable(alignedMap.error));
    QCOMPARE(alignedMap.table->alignments.size(), size_t(1));
    QCOMPARE(int(alignedMap.table->alignments[0]),
             int(TableAlignment::kCenter));

    const auto notTable = parse("| h |\nno delimiter\n");
    QVERIFY(notTable.document);
    const auto invalid = mapTable(*notTable.document, 0);
    QVERIFY(!invalid.table);
    QCOMPARE(invalid.error, QStringLiteral("Invalid table target"));

    const QByteArray crlf =
        "pre\r\n\r\n| h |\r\n| --- |\r\n| a |\r\n\r\npost\r\n";
    auto crlfParsed = parse(crlf);
    QVERIFY(crlfParsed.document);
    const auto crlfMap =
        mapTable(*crlfParsed.document,
                 find(*crlfParsed.document, NodeKind::kBlock, MD_BLOCK_TABLE));
    QVERIFY2(crlfMap.table, qPrintable(crlfMap.error));
    QCOMPARE(crlfMap.table->range.offset, qsizetype(7));
    QCOMPARE(crlfMap.table->range.size, qsizetype(23));
    QCOMPARE(crlfMap.table->header.cells[0].range.offset, qsizetype(9));
    QCOMPARE(crlfMap.table->body[0].cells[0].range.offset, qsizetype(25));

    const QByteArray eof = "| h |\n| --- |\n| a |";
    auto eofParsed = parse(eof);
    QVERIFY(eofParsed.document);
    const auto eofMap =
        mapTable(*eofParsed.document,
                 find(*eofParsed.document, NodeKind::kBlock, MD_BLOCK_TABLE));
    QVERIFY2(eofMap.table, qPrintable(eofMap.error));
    QCOMPARE(eofMap.table->range.offset, qsizetype(0));
    QCOMPARE(eofMap.table->range.size, qsizetype(eof.size()));

    const auto unicode = QStringLiteral("| h |\n| --- |\n| \u0430\U0001f600 |\n")
                             .toUtf8();
    auto unicodeParsed = parse(unicode);
    QVERIFY(unicodeParsed.document);
    const auto unicodeMap =
        mapTable(*unicodeParsed.document,
                 find(*unicodeParsed.document, NodeKind::kBlock, MD_BLOCK_TABLE));
    QVERIFY2(unicodeMap.table, qPrintable(unicodeMap.error));
    QCOMPARE(unicodeParsed.document->source.mid(
                 unicodeMap.table->body[0].cells[0].range.offset,
                 unicodeMap.table->body[0].cells[0].range.size),
             QStringLiteral("\u0430\U0001f600").toUtf8());

    const QByteArray duplicate =
        "| h |\n| --- |\n\n| h |\n| --- |\n";
    auto duplicateParsed = parse(duplicate);
    QVERIFY(duplicateParsed.document);
    std::vector<size_t> tables;
    for (size_t i = 0; i < duplicateParsed.document->nodes.size(); ++i) {
      if (duplicateParsed.document->nodes[i].type == MD_BLOCK_TABLE) {
        tables.push_back(i);
      }
    }
    QCOMPARE(tables.size(), size_t(2));
    const auto duplicateFirst =
        mapTable(*duplicateParsed.document, tables[0]);
    const auto duplicateSecond =
        mapTable(*duplicateParsed.document, tables[1]);
    QVERIFY(duplicateFirst.table);
    QVERIFY(duplicateSecond.table);
    QCOMPARE(duplicateFirst.table->range.offset, qsizetype(0));
    QCOMPARE(duplicateSecond.table->range.offset, qsizetype(15));
  }

  void tableRowCommands() {
    {
      auto parsed = parse("| h |\n| --- |\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result =
          editBytes(*parsed.document,
                    InsertTableRow{table, 0, {QByteArray("a")}});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("| h |\n| --- |\n| a |\n"));
    }
    {
      auto parsed = parse("| h |\n| --- |\n| a |\n| b |\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      for (const auto position : {size_t(0), size_t(1), size_t(2)}) {
        const auto result = editBytes(
            *parsed.document,
            InsertTableRow{table, position, {QByteArray("x")}});
        QVERIFY2(result.source, qPrintable(result.error));
        QByteArray expected = "| h |\n| --- |\n";
        if (position == 0) {
          expected += "| x |\n| a |\n| b |\n";
        } else if (position == 1) {
          expected += "| a |\n| x |\n| b |\n";
        } else {
          expected += "| a |\n| b |\n| x |\n";
        }
        QCOMPARE(*result.source, expected);
      }
    }
    {
      auto parsed = parse("| h |\n| --- |\n| a |\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result =
          editBytes(*parsed.document, RemoveTableRow{table, 0});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("| h |\n| --- |\n"));
    }
    {
      auto parsed = parse("| h |\n| --- |\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result =
          editBytes(*parsed.document, RemoveTableRow{table, 0});
      QVERIFY(!result.source);
      QVERIFY2(result.error.contains("Invalid table row index"),
               qPrintable(result.error));
    }
    {
      auto parsed = parse("| h | i |\n| --- | --- |\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      for (const auto& cells : {std::vector<QByteArray>{QByteArray("a")},
                               std::vector<QByteArray>{
                                   QByteArray("a"), QByteArray("b"),
                                   QByteArray("c")}}) {
        const auto result =
            editBytes(*parsed.document, InsertTableRow{table, 0, cells});
        QVERIFY(!result.source);
        QVERIFY2(result.error.contains("Invalid table row payload"),
                 qPrintable(result.error));
      }
    }
    {
      auto parsed = parse("| h |\n| --- |\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto insert = [&](const QByteArray& value) {
        return editBytes(*parsed.document,
                         InsertTableRow{table, 0, {value}});
      };
      QCOMPARE(*insert("").source, QByteArray("| h |\n| --- |\n|  |\n"));
      QCOMPARE(*insert("a b").source,
               QByteArray("| h |\n| --- |\n| a b |\n"));
      QCOMPARE(*insert("a|b").source,
               QByteArray("| h |\n| --- |\n| a\\|b |\n"));
      QCOMPARE(*insert("a\\b").source,
               QByteArray("| h |\n| --- |\n| a\\\\b |\n"));
      QCOMPARE(*insert("*x*").source,
               QByteArray("| h |\n| --- |\n| \\*x\\* |\n"));
      for (const auto& value : {QByteArray(" a"), QByteArray("a "),
                                QByteArray(" "), QByteArray("a\tb"),
                                QByteArray("a\nb"), QByteArray("a\rb")}) {
        const auto result = insert(value);
        QVERIFY(!result.source);
        QVERIFY(!result.error.isEmpty());
      }
      QByteArray withNul("a");
      withNul.append('\0');
      QVERIFY(!insert(withNul).source);
      QByteArray invalidUtf8("\xC3", 1);
      QVERIFY(!insert(invalidUtf8).source);
    }
    {
      const QByteArray source =
          "| h | i |\n| --- | --- |\n| a | b |\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto left = editBytes(
          *parsed.document,
          SetTableAlignment{table, 0, TableAlignment::kLeft});
      QVERIFY2(left.source, qPrintable(left.error));
      QCOMPARE(*left.source,
               QByteArray("| h | i |\n| :--- | --- |\n| a | b |\n"));
      const auto center = editBytes(
          *parsed.document,
          SetTableAlignment{table, 1, TableAlignment::kCenter});
      QVERIFY2(center.source, qPrintable(center.error));
      QCOMPARE(*center.source,
               QByteArray("| h | i |\n| --- | :---: |\n| a | b |\n"));
      const auto right = editBytes(
          *parsed.document,
          SetTableAlignment{table, 1, TableAlignment::kRight});
      QVERIFY2(right.source, qPrintable(right.error));
      QCOMPARE(*right.source,
               QByteArray("| h | i |\n| --- | ---: |\n| a | b |\n"));
      const auto noop =
          applyEdit(*parsed.document, snapshotToken(*parsed.document),
                    SetTableAlignment{table, 0, TableAlignment::kNone});
      QVERIFY(noop.document);
      QCOMPARE(noop.document->source, source);
      QCOMPARE(noop.document->revision, parsed.document->revision);
    }
    {
      const QByteArray source =
          "| h | i |\n| --- | --- |\n| `a\\|b` | *x* |\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result =
          editBytes(*parsed.document,
                    InsertTableRow{table, 1, {QByteArray("new"), QByteArray("r")}});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("| h | i |\n| --- | --- |\n| `a\\|b` | *x* |\n"
                          "| new | r |\n"));
    }
    {
      const QByteArray source =
          "[new]: /new \"N\"\n\n| h |\n| --- |\n| [cell][new] |\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result =
          editBytes(*parsed.document,
                    InsertTableRow{table, 1, {QByteArray("x")}});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("[new]: /new \"N\"\n\n| h |\n| --- |\n"
                          "| [cell][new] |\n| x |\n"));
      QCOMPARE(result.source->left(16), source.left(16));
      auto reparsed = parse(*result.source);
      QVERIFY(reparsed.document);
      const auto link =
          find(*reparsed.document, NodeKind::kSpan, MD_SPAN_A);
      QVERIFY(link < reparsed.document->nodes.size());
      QCOMPARE(reparsed.document->nodes[link].attributes["href"].toString(),
               QStringLiteral("/new"));
    }
    {
      auto parsed = parse("| h |\r\n| --- |\r\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result = editBytes(
          *parsed.document, InsertTableRow{table, 0, {QByteArray("a")}});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("| h |\r\n| --- |\r\n| a |\r\n"));
    }
    {
      auto parsed = parse("| h |\n| --- |\n| a |");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result = editBytes(
          *parsed.document, InsertTableRow{table, 1, {QByteArray("b")}});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("| h |\n| --- |\n| a |\n| b |"));
    }
  }

  void tableCommandGuards() {
    auto parsed = parse("| h |\n| --- |\n| a |\n", 4);
    QVERIFY(parsed.document);
    const auto& doc = *parsed.document;
    const auto table = find(doc, NodeKind::kBlock, MD_BLOCK_TABLE);
    QVERIFY(table < doc.nodes.size());
    const auto token = snapshotToken(doc);
    auto foreign = parse(doc.source, doc.revision);
    QVERIFY(foreign.document);
    for (const auto& command :
         std::vector<EditCommand>{
             InsertTableRow{table, 0, {QByteArray("x")}},
             RemoveTableRow{table, 0},
             SetTableAlignment{table, 0, TableAlignment::kCenter}}) {
      QVERIFY(!applyEdit(doc, snapshotToken(*foreign.document), command)
                   .document);
      auto invalidDigest = token;
      invalidDigest.digest[0] = char(invalidDigest.digest[0] ^ 1);
      QVERIFY(!applyEdit(doc, invalidDigest, command).document);
    }
    QVERIFY(!applyEdit(doc, token,
                       InsertTableRow{table, 2, {QByteArray("x")}})
                 .document);
    QVERIFY(!applyEdit(doc, token,
                       RemoveTableRow{table, 1})
                 .document);
    QVERIFY(!applyEdit(doc, token,
                       SetTableAlignment{table, 1, TableAlignment::kLeft})
                 .document);
    QVERIFY(!applyEdit(doc, token,
                       InsertTableRow{doc.nodes.size(), 0,
                                       {QByteArray("x")}})
                 .document);
    auto exhausted = parse(doc.source, std::numeric_limits<quint64>::max());
    QVERIFY(exhausted.document);
    auto overflow =
        applyEdit(*exhausted.document, snapshotToken(*exhausted.document),
                  InsertTableRow{
                      find(*exhausted.document, NodeKind::kBlock,
                           MD_BLOCK_TABLE),
                      0, {QByteArray("x")}});
    QVERIFY(!overflow.document);
    QVERIFY(overflow.error.contains("overflow"));
  }

  void tableColumnCommands() {
    {
      const QByteArray source =
          "| h | i |\n| --- | --- |\n| a | b |\n| c | d |\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const std::vector<QByteArray> expected{
          "| x | h | i |\n| :---: | --- | --- |\n| p | a | b |\n"
          "| q | c | d |\n",
          "| h | x | i |\n| --- | :---: | --- |\n| a | p | b |\n"
          "| c | q | d |\n",
          "| h | i | x |\n| --- | --- | :---: |\n| a | b | p |\n"
          "| c | d | q |\n"};
      for (size_t position = 0; position < 3; ++position) {
        const auto result = editBytes(
            *parsed.document,
            InsertTableColumn{table, position, QByteArray("x"),
                              {QByteArray("p"), QByteArray("q")},
                              TableAlignment::kCenter});
        QVERIFY2(result.source, qPrintable(result.error));
        QCOMPARE(*result.source, expected[position]);
      }
    }
    {
      auto parsed = parse("| h |\n| --- |\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result = editBytes(
          *parsed.document,
          InsertTableColumn{table, 1, QByteArray("x"), {},
                            TableAlignment::kNone});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("| h | x |\n| --- | --- |\n"));
    }
    {
      auto parsed =
          parse("| h | i |\n| --- | --- |\n| a | b |\n| c | d |\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result = editBytes(
          *parsed.document,
          InsertTableColumn{table, 0, QByteArray("x"), {QByteArray("p")},
                            TableAlignment::kNone});
      QVERIFY(!result.source);
      QVERIFY2(result.error.contains("Invalid table column payload"),
               qPrintable(result.error));
    }
    {
      const QByteArray source = "| h | i |\n| --- | --- |\n| a | b |\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      QCOMPARE(*editBytes(*parsed.document,
                          RemoveTableColumn{table, 0}).source,
               QByteArray("| i |\n| --- |\n| b |\n"));
      QCOMPARE(*editBytes(*parsed.document,
                          RemoveTableColumn{table, 1}).source,
               QByteArray("| h |\n| --- |\n| a |\n"));
    }
    {
      auto parsed = parse("| h |\n| --- |\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result = editBytes(*parsed.document,
                                    RemoveTableColumn{table, 0});
      QVERIFY(!result.source);
      QVERIFY2(result.error.contains("last table column"),
               qPrintable(result.error));
    }
    {
      auto parsed = parse("| h |\n| --- |\n| a |\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result = editBytes(*parsed.document, RemoveTable{table});
      QVERIFY2(result.source, qPrintable(result.error));
      QVERIFY(result.source->isEmpty());
    }
    {
      const QByteArray source = "pre\n\n| h |\n| --- |\n\npost\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result = editBytes(*parsed.document, RemoveTable{table});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("pre\n\n\npost\n"));
    }
    {
      const QByteArray source = "- a\n\n| h |\n| --- |\n\n- b\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result = editBytes(*parsed.document, RemoveTable{table});
      QVERIFY(!result.source);
      QVERIFY2(result.error.contains("semantics"), qPrintable(result.error));
    }
    {
      const QByteArray source =
          "| h | i |\n| --- | --- |\n| `a\\|b` | *x* |\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result = editBytes(
          *parsed.document,
          InsertTableColumn{table, 1, QByteArray("new"),
                            {QByteArray("r")}, TableAlignment::kNone});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("| h | new | i |\n| --- | --- | --- |\n"
                          "| `a\\|b` | r | *x* |\n"));
    }
    {
      auto parsed = parse("| h |\r\n| --- |\r\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result = editBytes(
          *parsed.document,
          InsertTableColumn{table, 1, QByteArray("x"), {},
                            TableAlignment::kNone});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("| h | x |\r\n| --- | --- |\r\n"));
    }
  }

  void nestedTableCommands() {
    {
      auto parsed = parse("> | h |\n> | --- |\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result =
          editBytes(*parsed.document,
                    InsertTableRow{table, 0, {QByteArray("a")}});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("> | h |\n> | --- |\n> | a |\n"));
      auto reparsed = parse(*result.source);
      QVERIFY(reparsed.document);
      const auto mapped = mapTable(
          *reparsed.document,
          find(*reparsed.document, NodeKind::kBlock, MD_BLOCK_TABLE));
      QVERIFY2(mapped.table, qPrintable(mapped.error));
      QCOMPARE(mapped.table->continuationPrefix, QByteArray("> "));
    }
    {
      auto parsed = parse("- | h |\n  | --- |\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result =
          editBytes(*parsed.document,
                    InsertTableRow{table, 0, {QByteArray("a")}});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("- | h |\n  | --- |\n  | a |\n"));
    }
    {
      auto parsed = parse("- p\n\n  | h |\n  | --- |\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result = editBytes(*parsed.document, RemoveTable{table});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("- p\n\n"));
      auto reparsed = parse(*result.source);
      QVERIFY(reparsed.document);
      const auto list =
          find(*reparsed.document, NodeKind::kBlock, MD_BLOCK_UL);
      QVERIFY(list < reparsed.document->nodes.size());
      QCOMPARE(reparsed.document->nodes[list].attributes["tight"].toBool(),
               true);
    }
    {
      auto parsed = parse("> p\n>\n> | h |\n> | --- |\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result = editBytes(*parsed.document, RemoveTable{table});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("> p\n>\n"));
    }
    {
      auto parsed = parse("- | h |\n  | --- |\n\n  p\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result = editBytes(*parsed.document, RemoveTable{table});
      QVERIFY(!result.source);
      QVERIFY2(result.error.contains("marker line"), qPrintable(result.error));
    }
    {
      auto parsed = parse("> | h |\n> | --- |\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result = editBytes(*parsed.document, RemoveTable{table});
      QVERIFY(!result.source);
      QVERIFY2(result.error.contains("cascade"), qPrintable(result.error));
    }
    {
      auto parsed = parse("> - p\n>\n>   | h |\n>   | --- |\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result =
          editBytes(*parsed.document,
                    InsertTableRow{table, 0, {QByteArray("a")}});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("> - p\n>\n>   | h |\n>   | --- |\n"
                          ">   | a |\n"));
    }
    {
      auto parsed = parse("> - p\n>\n>   > | h |\n>   > | --- |\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result =
          editBytes(*parsed.document,
                    InsertTableRow{table, 0, {QByteArray("a")}});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("> - p\n>\n>   > | h |\n>   > | --- |\n"
                          ">   > | a |\n"));
    }
    {
      auto parsed = parse("- | h |\n  | --- |\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto alignment = editBytes(
          *parsed.document,
          SetTableAlignment{table, 0, TableAlignment::kCenter});
      QVERIFY2(alignment.source, qPrintable(alignment.error));
      QCOMPARE(*alignment.source,
               QByteArray("- | h |\n  | :---: |\n"));
      const auto column = editBytes(
          *parsed.document,
          InsertTableColumn{table, 1, QByteArray("x"), {},
                            TableAlignment::kNone});
      QVERIFY2(column.source, qPrintable(column.error));
      QCOMPARE(*column.source,
               QByteArray("- | h | x |\n  | --- | --- |\n"));
    }
    {
      auto parsed = parse("- | h |\n  | --- |\n  | a |\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result = editBytes(*parsed.document, RemoveTableRow{table, 0});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("- | h |\n  | --- |\n"));
    }
    {
      auto parsed = parse("- | h | i |\n  | --- | --- |\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result = editBytes(*parsed.document,
                                    RemoveTableColumn{table, 0});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source, QByteArray("- | i |\n  | --- |\n"));
    }
    {
      const QByteArray source =
          "> | h | i |\n> | --- | --- |\n> | a | b |\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto alignment = editBytes(
          *parsed.document,
          SetTableAlignment{table, 0, TableAlignment::kCenter});
      QVERIFY2(alignment.source, qPrintable(alignment.error));
      QCOMPARE(*alignment.source,
               QByteArray("> | h | i |\n> | :---: | --- |\n"
                          "> | a | b |\n"));
      const auto column = editBytes(
          *parsed.document,
          InsertTableColumn{table, 1, QByteArray("x"), {QByteArray("r")},
                            TableAlignment::kNone});
      QVERIFY2(column.source, qPrintable(column.error));
      QCOMPARE(*column.source,
               QByteArray("> | h | x | i |\n> | --- | --- | --- |\n"
                          "> | a | r | b |\n"));
      const auto removed = editBytes(*parsed.document,
                                     RemoveTableColumn{table, 0});
      QVERIFY2(removed.source, qPrintable(removed.error));
      QCOMPARE(*removed.source,
               QByteArray("> | i |\n> | --- |\n> | b |\n"));
      const auto row = editBytes(
          *parsed.document,
          InsertTableRow{table, 1, {QByteArray("c"), QByteArray("d")}});
      QVERIFY2(row.source, qPrintable(row.error));
      QCOMPARE(*row.source,
               QByteArray("> | h | i |\n> | --- | --- |\n"
                          "> | a | b |\n> | c | d |\n"));
      const auto rowRemoved = editBytes(*parsed.document,
                                        RemoveTableRow{table, 0});
      QVERIFY2(rowRemoved.source, qPrintable(rowRemoved.error));
      QCOMPARE(*rowRemoved.source,
               QByteArray("> | h | i |\n> | --- | --- |\n"));
    }
    {
      auto parsed = parse("> | h |\r\n> | --- |\r\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result =
          editBytes(*parsed.document,
                    InsertTableRow{table, 0, {QByteArray("a")}});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("> | h |\r\n> | --- |\r\n> | a |\r\n"));
    }
    {
      auto parsed = parse("> | h |\n> | --- |\n| a |\n");
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result =
          editBytes(*parsed.document,
                    InsertTableRow{table, 0, {QByteArray("x")}});
      QVERIFY(!result.source);
    }
  }

  void nestedTableReferences() {
    {
      const QByteArray source =
          "[new]: /new \"N\"\n\n> | h |\n> | --- |\n> | [c][new] |\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result =
          editBytes(*parsed.document,
                    InsertTableRow{table, 1, {QByteArray("z")}});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("[new]: /new \"N\"\n\n> | h |\n> | --- |\n"
                          "> | [c][new] |\n> | z |\n"));
      QCOMPARE(result.source->left(16), source.left(16));
      auto reparsed = parse(*result.source);
      QVERIFY(reparsed.document);
      const auto link =
          find(*reparsed.document, NodeKind::kSpan, MD_SPAN_A);
      QVERIFY(link < reparsed.document->nodes.size());
      QCOMPARE(reparsed.document->nodes[link].attributes["href"].toString(),
               QStringLiteral("/new"));
    }
    {
      const QByteArray source =
          "[new]: /new \"N\"\n\n- p\n\n  | h |\n  | --- |\n  | [c][new] |\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result =
          editBytes(*parsed.document,
                    InsertTableRow{table, 1, {QByteArray("z")}});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("[new]: /new \"N\"\n\n- p\n\n  | h |\n"
                          "  | --- |\n  | [c][new] |\n  | z |\n"));
    }
    {
      const QByteArray source =
          "[new]: /new \"N\"\n\n- p\n\n  | h |\n  | --- |\n  | [c][new] |\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result = editBytes(*parsed.document, RemoveTable{table});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("[new]: /new \"N\"\n\n- p\n\n"));
      auto reparsed = parse(*result.source);
      QVERIFY(reparsed.document);
      QVERIFY(find(*reparsed.document, NodeKind::kSpan, MD_SPAN_A) >=
              reparsed.document->nodes.size());
      const auto definitions = parse(*result.source);
      QVERIFY(definitions.document);
      QVERIFY(definitions.document->source.contains("[new]: /new"));
    }
    {
      const QByteArray source =
          "[new]: /new \"N\"\n\n> | h |\n> | --- |\n> | [c][new] |\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result = editBytes(
          *parsed.document,
          RemoveTableRow{table, 0});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               QByteArray("[new]: /new \"N\"\n\n> | h |\n> | --- |\n"));
      QVERIFY(result.source->contains("[new]: /new"));
    }
    for (const QByteArray& reference :
         {QByteArray("[c][]"), QByteArray("[c]"),
          QByteArray("![alt][c]")}) {
      const QByteArray source =
          "[c]: /new \"N\"\n\n> | h |\n> | --- |\n> | " + reference + " |\n";
      auto parsed = parse(source);
      QVERIFY(parsed.document);
      const auto table =
          find(*parsed.document, NodeKind::kBlock, MD_BLOCK_TABLE);
      const auto result =
          editBytes(*parsed.document,
                    InsertTableRow{table, 1, {QByteArray("z")}});
      QVERIFY2(result.source, qPrintable(result.error));
      QCOMPARE(*result.source,
               "[c]: /new \"N\"\n\n> | h |\n> | --- |\n> | " + reference +
                   " |\n> | z |\n");
      auto reparsed = parse(*result.source);
      QVERIFY(reparsed.document);
      const auto span =
          find(*reparsed.document, NodeKind::kSpan,
               reference.startsWith("!") ? MD_SPAN_IMG : MD_SPAN_A);
      QVERIFY(span < reparsed.document->nodes.size());
      const auto& attributes = reparsed.document->nodes[span].attributes;
      QCOMPARE(attributes["src"].toString().isEmpty()
                   ? attributes["href"].toString()
                   : attributes["src"].toString(),
               QStringLiteral("/new"));
    }
  }

  void imageAltEdits() {
    const QByteArray inlineImage = "![before](/image \"T\")\n";
    auto parsed = parse(inlineImage);
    QVERIFY(parsed.document);
    const auto before =
        find(*parsed.document, NodeKind::kText, MD_TEXT_NORMAL, "before");
    QVERIFY(before < parsed.document->nodes.size());
    QVERIFY(!parsed.document->nodes[before].parent ||
            parsed.document->nodes[*parsed.document->nodes[before].parent]
                    .type == MD_SPAN_IMG);
    auto edited = editBytes(*parsed.document, ReplaceText{before, "after"});
    QVERIFY2(edited.source, qPrintable(edited.error));
    QCOMPARE(*edited.source, QByteArray("![after](/image \"T\")\n"));
    auto reparsed = parse(*edited.source);
    QVERIFY(reparsed.document);
    const auto image =
        find(*reparsed.document, NodeKind::kSpan, MD_SPAN_IMG);
    QVERIFY(image < reparsed.document->nodes.size());
    QCOMPARE(reparsed.document->nodes[image].attributes["src"].toString(),
             QStringLiteral("/image"));
    QCOMPARE(reparsed.document->nodes[image].attributes["title"].toString(),
             QStringLiteral("T"));

    const QByteArray reference =
        "![before][id]\n\n[id]: /img \"I\"\n";
    auto referenceParsed = parse(reference);
    QVERIFY(referenceParsed.document);
    const auto referenceText =
        find(*referenceParsed.document, NodeKind::kText, MD_TEXT_NORMAL,
             "before");
    QVERIFY(referenceText < referenceParsed.document->nodes.size());
    auto referenceEdit =
        editBytes(*referenceParsed.document, ReplaceText{referenceText, "after"});
    QVERIFY2(referenceEdit.source, qPrintable(referenceEdit.error));
    QCOMPARE(*referenceEdit.source,
             QByteArray("![after][id]\n\n[id]: /img \"I\"\n"));
    auto referenceReparsed = parse(*referenceEdit.source);
    QVERIFY(referenceReparsed.document);
    const auto referenceImage =
        find(*referenceReparsed.document, NodeKind::kSpan, MD_SPAN_IMG);
    QVERIFY(referenceImage < referenceReparsed.document->nodes.size());
    QCOMPARE(referenceReparsed.document->nodes[referenceImage]
                 .attributes["src"]
                 .toString(),
             QStringLiteral("/img"));
    QCOMPARE(referenceReparsed.document->nodes[referenceImage]
                 .attributes["title"]
                 .toString(),
             QStringLiteral("I"));

    const QByteArray unsafe = "![alt](javascript:ignored)\n";
    auto unsafeParsed = parse(unsafe);
    QVERIFY(unsafeParsed.document);
    const auto unsafeText =
        find(*unsafeParsed.document, NodeKind::kText, MD_TEXT_NORMAL, "alt");
    QVERIFY(unsafeText < unsafeParsed.document->nodes.size());
    auto unsafeEdit = editBytes(*unsafeParsed.document,
                                ReplaceText{unsafeText, "safe"});
    QVERIFY2(unsafeEdit.source, qPrintable(unsafeEdit.error));
    QCOMPARE(*unsafeEdit.source, QByteArray("![safe](javascript:ignored)\n"));

    auto invalidImage = parse(inlineImage);
    QVERIFY(invalidImage.document);
    const auto invalidText = find(*invalidImage.document, NodeKind::kText,
                                  MD_TEXT_NORMAL, "before");
    for (const auto& value : {QByteArray{}, QByteArray("two\nlines"),
                              QByteArray("two\rlines")}) {
      const auto rejected = editBytes(*invalidImage.document,
                                      ReplaceText{invalidText, value});
      QVERIFY(!rejected.source);
      QVERIFY(!rejected.error.isEmpty());
    }
  }

  void wrapInLink_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<QByteArray>("text");
    QTest::addColumn<int>("offset");
    QTest::addColumn<int>("size");
    QTest::addColumn<QByteArray>("url");
    QTest::addColumn<QByteArray>("expected");
    QTest::newRow("full")
        << QByteArray("alpha beta\n") << QByteArray("alpha beta") << 0 << 5
        << QByteArray("https://example.test")
        << QByteArray("[alpha](<https://example.test>) beta\n");
    QTest::newRow("partial")
        << QByteArray("alpha beta\n") << QByteArray("alpha beta") << 6 << 4
        << QByteArray("https://example.test")
        << QByteArray("alpha [beta](<https://example.test>)\n");
    QTest::newRow("paren-url")
        << QByteArray("alpha\n") << QByteArray("alpha") << 0 << 5
        << QByteArray("https://example.test/a(b)")
        << QByteArray("[alpha](<https://example.test/a(b)>)\n");
    QTest::newRow("formatted")
        << QByteArray("**alpha**\n") << QByteArray("alpha") << 0 << 5
        << QByteArray("https://example.test")
        << QByteArray("**[alpha](<https://example.test>)**\n");
  }

  void wrapInLink() {
    QFETCH(QByteArray, source);
    QFETCH(QByteArray, text);
    QFETCH(int, offset);
    QFETCH(int, size);
    QFETCH(QByteArray, url);
    QFETCH(QByteArray, expected);
    const auto parsed = parse(source);
    QVERIFY(parsed.document);
    const auto node =
        find(*parsed.document, NodeKind::kText, MD_TEXT_NORMAL, text);
    QVERIFY(node < parsed.document->nodes.size());
    const auto result = editBytes(
        *parsed.document,
        WrapInLink{node, {offset, size}, url});
    QVERIFY2(result.source, qPrintable(result.error));
    QCOMPARE(*result.source, expected);
  }

  void wrapInLinkRejected() {
    const auto parsed = parse("alpha [link](/keep)\n");
    QVERIFY(parsed.document);
    const auto linkText =
        find(*parsed.document, NodeKind::kText, MD_TEXT_NORMAL, "link");
    QVERIFY(linkText < parsed.document->nodes.size());
    for (const auto& command :
         std::vector<EditCommand>{
             WrapInLink{linkText, {0, 4}, "https://example.test"},
             WrapInLink{linkText, {0, 0}, "https://example.test"},
             WrapInLink{linkText, {0, 4}, ""},
             WrapInLink{linkText, {0, 4}, "bad url"},
             WrapInLink{linkText, {0, 4}, "bad<url"}}) {
      QVERIFY(!editBytes(*parsed.document, command).source);
    }
    const auto image = parse("![alt](/keep)\n");
    QVERIFY(image.document);
    const auto alt = find(*image.document, NodeKind::kText, MD_TEXT_NORMAL, "alt");
    QVERIFY(alt < image.document->nodes.size());
    QVERIFY(!editBytes(*image.document,
                       WrapInLink{alt, {0, 3}, "https://example.test"})
                 .source);
  }

  void wrapInLinkStale() {
    const auto parsed = parse("alpha\n");
    QVERIFY(parsed.document);
    const auto node =
        find(*parsed.document, NodeKind::kText, MD_TEXT_NORMAL, "alpha");
    const auto foreign = parse("beta\n");
    QVERIFY(foreign.document);
    QVERIFY(!applyEdit(*parsed.document, snapshotToken(*foreign.document),
                       WrapInLink{node, {0, 5}, "https://example.test"})
                 .document);
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
