#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextCursor>
#include <QTextDocument>
#include <QtTest/QtTest>
#include <cstdlib>
#include <memory>

namespace {

using Node = std::unique_ptr<cmark_node, decltype(&cmark_node_free)>;
using Parser = std::unique_ptr<cmark_parser, decltype(&cmark_parser_free)>;
using Buffer = std::unique_ptr<char, decltype(&std::free)>;

Node parse(const QByteArray& source) {
  Parser parser(cmark_parser_new(CMARK_OPT_DEFAULT), &cmark_parser_free);
  if (!parser) {
    qWarning("Cannot allocate cmark parser");
    return Node(nullptr, &cmark_node_free);
  }
  for (const auto* name :
       {"table", "strikethrough", "tasklist", "autolink", "tagfilter"}) {
    auto* extension = cmark_find_syntax_extension(name);
    if (!extension ||
        !cmark_parser_attach_syntax_extension(parser.get(), extension)) {
      qWarning("Cannot attach cmark extension: %s", name);
      return Node(nullptr, &cmark_node_free);
    }
  }
  cmark_parser_feed(parser.get(), source.constData(),
                    static_cast<size_t>(source.size()));
  Node root(cmark_parser_finish(parser.get()), &cmark_node_free);
  if (!root) {
    qWarning("Cannot finish cmark parse");
  }
  return root;
}

QJsonObject describe(cmark_node* node) {
  const QByteArray type(cmark_node_get_type_string(node));
  QJsonObject result{{"type", QString::fromUtf8(type)}};
  if (const auto* literal = cmark_node_get_literal(node)) {
    result["literal"] = QString::fromUtf8(literal);
  }
  switch (cmark_node_get_type(node)) {
    case CMARK_NODE_HEADING:
      result["level"] = cmark_node_get_heading_level(node);
      break;
    case CMARK_NODE_LIST:
      result["list_type"] = cmark_node_get_list_type(node);
      result["start"] = cmark_node_get_list_start(node);
      result["tight"] = bool(cmark_node_get_list_tight(node));
      break;
    case CMARK_NODE_CODE_BLOCK:
      result["info"] = QString::fromUtf8(cmark_node_get_fence_info(node));
      break;
    case CMARK_NODE_LINK:
    case CMARK_NODE_IMAGE:
      result["url"] = QString::fromUtf8(cmark_node_get_url(node));
      result["title"] = QString::fromUtf8(cmark_node_get_title(node));
      break;
    default:
      break;
  }
  if (type == "table") {
    const auto count = cmark_gfm_extensions_get_table_columns(node);
    const auto* alignments = cmark_gfm_extensions_get_table_alignments(node);
    QJsonArray values;
    for (uint16_t i = 0; i < count; ++i) {
      values.append(alignments[i]);
    }
    result["alignments"] = values;
  } else if (type == "table_header" || type == "table_row") {
    result["header"] = bool(cmark_gfm_extensions_get_table_row_is_header(node));
  } else if (type == "tasklist") {
    result["checked"] = cmark_gfm_extensions_get_tasklist_item_checked(node);
  }
  QJsonArray children;
  for (auto* child = cmark_node_first_child(node); child;
       child = cmark_node_next(child)) {
    // Autolinks can leave an empty text node that has no document semantics.
    if (cmark_node_get_type(child) == CMARK_NODE_TEXT &&
        QByteArray(cmark_node_get_literal(child)).isEmpty()) {
      continue;
    }
    children.append(describe(child));
  }
  result["children"] = children;
  return result;
}

QByteArray semantics(cmark_node* root) {
  return QJsonDocument(describe(root)).toJson(QJsonDocument::Compact);
}

cmark_node* findType(cmark_node* node, const QByteArray& type) {
  if (type == cmark_node_get_type_string(node)) {
    return node;
  }
  for (auto* child = cmark_node_first_child(node); child;
       child = cmark_node_next(child)) {
    if (auto* found = findType(child, type)) {
      return found;
    }
  }
  return nullptr;
}

cmark_node* findEditableLiteral(cmark_node* node) {
  const auto* literal = cmark_node_get_literal(node);
  if (literal && QByteArray(literal).contains("before")) {
    return node;
  }
  for (auto* child = cmark_node_first_child(node); child;
       child = cmark_node_next(child)) {
    if (auto* found = findEditableLiteral(child)) {
      return found;
    }
  }
  return nullptr;
}

class OfflineDocument : public QTextDocument {
 protected:
  QVariant loadResource(int, const QUrl& url) override {
    qInfo() << "Resource loading blocked in spike:" << url;
    return {};
  }
};

}  // namespace

class GfmRoundTripTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() {
    cmark_gfm_core_extensions_ensure_registered();
    qInfo() << "cmark-gfm:" << cmark_version_string() << "Qt:" << qVersion();
  }

  void cmarkRoundTrip_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<QByteArray>("requiredType");
    QTest::newRow("headings")
        << QByteArray("before\n======\n") << QByteArray("heading");
    QTest::newRow("emphasis")
        << QByteArray("_before_ and __strong__\n") << QByteArray("emph");
    QTest::newRow("strikethrough")
        << QByteArray("~~before~~\n") << QByteArray("strikethrough");
    QTest::newRow("table")
        << QByteArray(
               "| Left | Mid | Right |\n| :--- | :---: | ---: |\n"
               "| before | **bold** | 3 |\n")
        << QByteArray("table");
    QTest::newRow("table-code-pipe") << QByteArray(
                                            "| Code | Text |\n| --- | --- |\n"
                                            "| `a\\|b` | before |\n")
                                     << QByteArray("table");
    QTest::newRow("tasks") << QByteArray("* [ ] before\n* [x] done\n")
                           << QByteArray("tasklist");
    QTest::newRow("fenced-code")
        << QByteArray("~~~cpp\nbefore\n~~~\n") << QByteArray("code_block");
    QTest::newRow("raw-html-block")
        << QByteArray("<div data-kind=\"opaque\">before</div>\n")
        << QByteArray("html_block");
    QTest::newRow("raw-html-inline")
        << QByteArray("Keep <span data-kind=\"opaque\">before</span>.\n")
        << QByteArray("html_inline");
    QTest::newRow("unknown")
        << QByteArray(":::note\nbefore\n:::\n") << QByteArray("text");
    QTest::newRow("malformed")
        << QByteArray("before *unclosed [link](\n") << QByteArray("text");
    QTest::newRow("unclosed-fence")
        << QByteArray("```text\nbefore\n") << QByteArray("code_block");
    QTest::newRow("links")
        << QByteArray(
               "before [link][id] and ![alt](missing.png \"title\").\n\n"
               "[id]: https://example.test \"title\"\n")
        << QByteArray("link");
    QTest::newRow("autolinks")
        << QByteArray("before www.example.com and user@example.com\n")
        << QByteArray("link");
    QTest::newRow("nested") << QByteArray("> - [ ] ~~before~~ and **bold**\n")
                            << QByteArray("item");
    QTest::newRow("unicode")
        << QStringLiteral(
               "before \u041f\u0440\u0438\u0432\u0435\u0442 \U0001f600\n")
               .toUtf8()
        << QByteArray("text");
  }

  void cmarkRoundTrip() {
    QFETCH(QByteArray, source);
    QFETCH(QByteArray, requiredType);
    auto root = parse(source);
    QVERIFY(root);
    QVERIFY2(findType(root.get(), requiredType), requiredType.constData());

    Buffer unchanged(cmark_render_commonmark(root.get(), CMARK_OPT_DEFAULT, 0),
                     &std::free);
    QVERIFY(unchanged);
    auto reparsed = parse(QByteArray(unchanged.get()));
    QVERIFY(reparsed);
    QCOMPARE(semantics(reparsed.get()), semantics(root.get()));

    auto* literal = findEditableLiteral(root.get());
    QVERIFY(literal);
    QByteArray editedLiteral(cmark_node_get_literal(literal));
    editedLiteral.replace("before", "after");
    QVERIFY(cmark_node_set_literal(literal, editedLiteral.constData()));
    QByteArray expectedSource(source);
    expectedSource.replace("before", "after");
    auto expected = parse(expectedSource);
    QVERIFY(expected);
    QCOMPARE(semantics(root.get()), semantics(expected.get()));

    Buffer serialized(cmark_render_commonmark(root.get(), CMARK_OPT_DEFAULT, 0),
                      &std::free);
    QVERIFY(serialized);
    auto edited = parse(QByteArray(serialized.get()));
    QVERIFY(edited);
    QCOMPARE(semantics(edited.get()), semantics(expected.get()));
    qInfo().noquote() << "cmark edited output:\n" << serialized.get();
    qInfo() << "Original bytes preserved without edit:"
            << (source == QByteArray(unchanged.get()));
  }

  void qtCharacterization_data() { cmarkRoundTrip_data(); }

  void qtCharacterization() {
    QFETCH(QByteArray, source);
    QFETCH(QByteArray, requiredType);
    OfflineDocument document;
    document.setMarkdown(QString::fromUtf8(source),
                         QTextDocument::MarkdownDialectGitHub);
    qInfo().noquote() << "Qt unchanged output:\n"
                      << document.toMarkdown(
                             QTextDocument::MarkdownDialectGitHub);
    auto cursor = document.find(QStringLiteral("before"));
    QVERIFY(!cursor.isNull());
    cursor.insertText(QStringLiteral("after"));
    const auto serialized =
        document.toMarkdown(QTextDocument::MarkdownDialectGitHub).toUtf8();
    QVERIFY(!serialized.isEmpty());
    QByteArray expectedSource(source);
    expectedSource.replace("before", "after");
    auto expected = parse(expectedSource);
    auto actual = parse(serialized);
    QVERIFY(expected);
    QVERIFY(actual);
    const bool preserved = semantics(actual.get()) == semantics(expected.get());
    qInfo().noquote() << "Qt edited output:\n" << serialized;
    qInfo() << "Qt matches cmark semantic signature:" << preserved;
    QVERIFY(serialized.contains("after"));
    QVERIFY(!serialized.contains("before"));
    // Qt characterization is not a claim of conformance across Qt versions.
    if (requiredType == "html_block" || requiredType == "html_inline") {
      QVERIFY(!preserved);
      QVERIFY(!serialized.contains("data-kind"));
    }
  }

  void structuralEdits() {
    auto root = parse(
        "# Title\n\n| Left | Right |\n| :--- | ---: |\n| a | b |\n\n"
        "- [ ] task\n");
    QVERIFY(root);
    auto* heading = findType(root.get(), "heading");
    auto* table = findType(root.get(), "table");
    auto* task = findType(root.get(), "tasklist");
    QVERIFY(heading);
    QVERIFY(table);
    QVERIFY(task);
    QVERIFY(cmark_node_set_heading_level(heading, 2));
    uint8_t alignments[] = {'c', 'l'};
    // Deliberately exercise the public setter; LSan detects its replacement
    // leak.
    QVERIFY(cmark_gfm_extensions_set_table_alignments(table, 2, alignments));
    QVERIFY(cmark_gfm_extensions_set_tasklist_item_checked(task, true));
    auto expected = parse(
        "## Title\n\n| Left | Right |\n| :---: | :--- |\n"
        "| a | b |\n\n- [x] task\n");
    QVERIFY(expected);
    QCOMPARE(semantics(root.get()), semantics(expected.get()));
    Buffer serialized(cmark_render_commonmark(root.get(), CMARK_OPT_DEFAULT, 0),
                      &std::free);
    QVERIFY(serialized);
    auto actual = parse(QByteArray(serialized.get()));
    QVERIFY(actual);
    QCOMPARE(semantics(actual.get()), semantics(expected.get()));
  }

  void emptyDocument() {
    auto root = parse({});
    QVERIFY(root);
    QVERIFY(!cmark_node_first_child(root.get()));
    Buffer serialized(cmark_render_commonmark(root.get(), CMARK_OPT_DEFAULT, 0),
                      &std::free);
    QVERIFY(serialized);
    auto actual = parse(QByteArray(serialized.get()));
    QVERIFY(actual);
    QCOMPARE(semantics(actual.get()), semantics(root.get()));
  }

  void semanticComparisonRetainsSoftBreaks() {
    auto split = parse("hello\nworld\n");
    auto joined = parse("hello world\n");
    QVERIFY(split);
    QVERIFY(joined);
    QVERIFY(semantics(split.get()) != semantics(joined.get()));
  }

  void nestedTaskIsNotRecognized() {
    auto root = parse("> - [ ] task\n");
    QVERIFY(root);
    QVERIFY(findType(root.get(), "item"));
    QVERIFY(!findType(root.get(), "tasklist"));
    qInfo("Known cmark limitation: quoted task is parsed as an ordinary item");
  }

  void orderedTasksLoseListKind() {
    auto root = parse("3. [ ] before\n4. [x] done\n");
    QVERIFY(root);
    QVERIFY(findType(root.get(), "tasklist"));
    auto* list = findType(root.get(), "list");
    QVERIFY(list);
    QCOMPARE(cmark_node_get_list_type(list), CMARK_ORDERED_LIST);
    Buffer serialized(cmark_render_commonmark(root.get(), CMARK_OPT_DEFAULT, 0),
                      &std::free);
    QVERIFY(serialized);
    auto actual = parse(QByteArray(serialized.get()));
    QVERIFY(actual);
    auto* actualList = findType(actual.get(), "list");
    QVERIFY(actualList);
    QCOMPARE(cmark_node_get_list_type(actualList), CMARK_BULLET_LIST);
    QVERIFY(semantics(root.get()) != semantics(actual.get()));
    qInfo().noquote() << "Known cmark loss, ordered task output:\n"
                      << serialized.get();
  }
};

QTEST_MAIN(GfmRoundTripTest)

#include "round_trip_test.moc"
