#ifndef MARKPAD_SRC_MARKDOWN_DOCUMENT_H_
#define MARKPAD_SRC_MARKDOWN_DOCUMENT_H_

#include <md4c.h>

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QUuid>
#include <optional>
#include <variant>
#include <vector>

namespace markpad::markdown {

struct SourceRange {
  qsizetype offset;
  qsizetype size;
};

enum class NodeKind { kBlock, kSpan, kText };

struct Node {
  NodeKind kind;
  int type;
  QJsonObject attributes;
  QByteArray text;
  std::vector<size_t> children;
  std::optional<SourceRange> source;
  std::optional<size_t> parent;
};

struct Document {
  QByteArray source;
  std::vector<Node> nodes;
  quint64 revision = 0;
  QUuid identity;
};

struct ParseResult {
  std::optional<Document> document;
  QString error;
};

ParseResult parse(QByteArray source, quint64 revision = 0);
QByteArray signature(const Document& document);

struct BlockSource {
  size_t node;
  SourceRange range;
  SourceRange content;
};

struct SnapshotToken {
  quint64 revision;
  QByteArray digest;
  QUuid identity;
};

struct EvidenceEnvelope {
  size_t node;
  std::optional<size_t> parent;
  SourceRange range;
};

// Known own/descendant bytes only, NOT full syntactic ownership of a container.
std::vector<EvidenceEnvelope> evidenceEnvelopes(const Document& document);
// Partial map: single-line P/ATX, top-level or under explicit quote prefixes.
std::vector<BlockSource> mapBlocks(const Document& document);
struct ListItemSource {
  size_t node;
  SourceRange range;
  // Discontiguous content after marker/continuation prefixes, including EOLs.
  std::vector<SourceRange> contentLines;
};
struct ListSource {
  size_t node;
  SourceRange range;
  std::vector<ListItemSource> items;
  // Prefix for lines after the first item line (parent indentation/quote).
  QByteArray continuationPrefix;
  QByteArray blankLinePrefix;
};
struct ListMapResult {
  std::optional<ListSource> list;
  QString error;
};
// Exact canonical top-level list ownership, not an evidence envelope.
ListMapResult mapList(const Document& document, size_t node);

struct QuoteSource {
  size_t node;
  SourceRange range;
  QByteArray continuationPrefix;
  QByteArray blankLinePrefix;
};
struct QuoteMapResult {
  std::optional<QuoteSource> quote;
  QString error;
};
// Exact top-level quote ownership, not an evidence envelope.
QuoteMapResult mapQuote(const Document& document, size_t node);
SnapshotToken snapshotToken(const Document& document);

struct ReplaceText {
  size_t node;
  QByteArray text;
};
struct SetTaskChecked {
  size_t node;
  bool checked;
};
struct SetHeadingLevel {
  size_t node;
  int level;  // 0 means paragraph, 1..6 means heading.
};
enum class InlineStyle { kBold, kItalic, kStrikethrough };
struct ToggleInlineStyle {
  size_t node;
  SourceRange textRange;  // UTF-8 bytes relative to the normal text node.
  InlineStyle style;
};

enum class TaskState { kNone, kUnchecked, kChecked };

struct InsertListItem {
  size_t list;
  size_t position;  // 0..item count
  QByteArray blocks;
};
struct RemoveListItem {
  size_t item;
};
struct SetListKind {
  size_t list;
  bool ordered;
  int start;  // Required for ordered; ignored for unordered.
};
struct SetItemTask {
  size_t item;
  TaskState state;
};
struct WrapInQuote {
  size_t parent;
  size_t first;  // First selected block child.
  size_t count;  // Number of contiguous block children.
};
struct UnwrapQuote {
  size_t quote;
};
struct IndentListItem {
  size_t item;
};
struct OutdentListItem {
  size_t item;
};

using EditCommand =
    std::variant<ReplaceText, SetTaskChecked, SetHeadingLevel, ToggleInlineStyle,
                 InsertListItem, RemoveListItem, SetListKind, SetItemTask,
                 WrapInQuote, UnwrapQuote, IndentListItem, OutdentListItem>;

// Edits return a reparsed snapshot; no-op preserves identity, revision and
// bytes.
ParseResult applyEdit(const Document& document, const SnapshotToken& token,
                      const EditCommand& command);

}  // namespace markpad::markdown

#endif  // MARKPAD_SRC_MARKDOWN_DOCUMENT_H_
