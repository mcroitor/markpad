#include <QtTest/QtTest>

#include "markdown_document.h"

namespace markpad {
namespace {

std::optional<size_t> findTextNode(const markdown::Document& document,
                                   const QByteArray& text) {
  for (size_t i = 0; i < document.nodes.size(); ++i) {
    const auto& node = document.nodes[i];
    if (node.kind == markdown::NodeKind::kText && node.text == text) {
      return i;
    }
  }
  return std::nullopt;
}

std::optional<size_t> findListItem(const markdown::Document& document,
                                   const QByteArray& text) {
  const auto textNode = findTextNode(document, text);
  if (!textNode || !document.nodes[*textNode].parent) {
    return std::nullopt;
  }
  const auto parent = *document.nodes[*textNode].parent;
  if (document.nodes[parent].type != MD_BLOCK_LI) {
    return std::nullopt;
  }
  return parent;
}

}  // namespace

class MarkdownEditTest : public QObject {
  Q_OBJECT

 private slots:
  void removesLineBreaks() {
    const QByteArray source = "a\nb\n\nc  \nd\n\ne\\\nf\n";
    const auto parsed = markdown::parse(source);
    QVERIFY(parsed.document);
    auto document = *parsed.document;
    const auto soft = findTextNode(document, "\n");
    QVERIFY(soft);
    const auto token = markdown::snapshotToken(document);
    auto result = markdown::applyEdit(
        document, token, markdown::RemoveLineBreak{*soft});
    QVERIFY2(result.document, qPrintable(result.error));
    QCOMPARE(result.document->source, QByteArray("a b\n\nc  \nd\n\ne\\\nf\n"));

    const auto hard = findTextNode(*result.document, "\n");
    QVERIFY(hard);
    const auto hardResult = markdown::applyEdit(
        *result.document, markdown::snapshotToken(*result.document),
        markdown::RemoveLineBreak{*hard});
    QVERIFY2(hardResult.document, qPrintable(hardResult.error));
    QCOMPARE(hardResult.document->source, QByteArray("a b\n\nc d\n\ne\\\nf\n"));

    const auto finalDocument = hardResult.document;
    const auto escaped = findTextNode(*finalDocument, "\n");
    QVERIFY(escaped);
    const auto escapedResult = markdown::applyEdit(
        *finalDocument, markdown::snapshotToken(*finalDocument),
        markdown::RemoveLineBreak{*escaped});
    QVERIFY2(escapedResult.document, qPrintable(escapedResult.error));
    QCOMPARE(escapedResult.document->source, QByteArray("a b\n\nc d\n\ne f\n"));
  }

  void detachesListItems() {
    const auto run = [](const QByteArray& source, const QByteArray& item,
                        const QByteArray& expected) {
      const auto parsed = markdown::parse(source);
      if (!parsed.document) {
        return false;
      }
      const auto node = findListItem(*parsed.document, item);
      if (!node) {
        return false;
      }
      auto result = markdown::applyEdit(
          *parsed.document, markdown::snapshotToken(*parsed.document),
          markdown::DetachListItem{*node});
      return result.document && result.document->source == expected;
    };
    QVERIFY(run("- a\n- b\n- c\n", "b", "- a\n\nb\n\n- c\n"));
    QVERIFY(run("- b\n- c\n", "b", "b\n\n- c\n"));
    QVERIFY(run("- a\n- b\n", "b", "- a\n\nb\n"));
    QVERIFY(run("- only\n", "only", "only\n"));
    QVERIFY(run("- [ ] task\n- b\n", "task", "task\n\n- b\n"));
    QVERIFY(run("1. a\n2. b\n3. c\n", "b", "1. a\n\nb\n\n3. c\n"));
  }

  void rejectsUnsupportedDetach() {
    const auto parsed = markdown::parse("- a\n  - b\n  - c\n- d\n");
    QVERIFY(parsed.document);
    const auto nested = findListItem(*parsed.document, "b");
    QVERIFY(nested);
    const auto result = markdown::applyEdit(
        *parsed.document, markdown::snapshotToken(*parsed.document),
        markdown::DetachListItem{*nested});
    QVERIFY(!result.document);
    QVERIFY(!result.error.isEmpty());
  }
};

}  // namespace markpad

QTEST_MAIN(markpad::MarkdownEditTest)
#include "markdown_edit_test.moc"
