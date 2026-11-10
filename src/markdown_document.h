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
struct FencedCodeBodySource {
  SourceRange block;
  SourceRange body;
  QByteArray eol;
  bool closed = false;
};
// Top-level fenced code only; body excludes opening/closing fence lines.
std::optional<FencedCodeBodySource> mapFencedCodeBody(const Document& document,
                                                      size_t node,
                                                      QString& error);
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

enum class TableAlignment { kNone = 0, kLeft = 1, kCenter = 2, kRight = 3 };

struct TableCellSource {
  size_t node;
  SourceRange range;  // Exact raw cell fragment without structural whitespace.
};
struct TableRowSource {
  size_t node;
  SourceRange range;  // Includes the row EOL when present.
  std::vector<TableCellSource> cells;
};
struct TableSource {
  size_t node;
  SourceRange range;
  size_t columns = 0;
  std::vector<TableAlignment> alignments;
  TableRowSource header;
  SourceRange delimiterRange;
  std::vector<SourceRange> delimiterCells;
  std::vector<TableRowSource> body;
  QByteArray continuationPrefix;
  QByteArray blankLinePrefix;
};
struct TableMapResult {
  std::optional<TableSource> table;
  QString error;
};
// Exact top-level GFM table ownership, not an evidence envelope.
TableMapResult mapTable(const Document& document, size_t node);
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
struct WrapInLink {
  size_t node;
  SourceRange textRange;  // UTF-8 bytes relative to the normal text node.
  QByteArray url;
};
struct InsertImage {
  size_t node;
  SourceRange textRange;  // UTF-8 bytes relative to the normal text node.
  QByteArray src;
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
struct WrapInList {
  size_t parent;  // Only the document root is supported in this slice.
  size_t first;   // Paragraph child position.
  bool ordered;
  int start;  // Required for ordered; ignored for unordered.
};
struct ReplaceCodeBody {
  size_t node;
  QByteArray body;  // UTF-8; line endings are normalized to the block EOL.
};
struct IndentListItem {
  size_t item;
};
struct OutdentListItem {
  size_t item;
};
struct InsertTableRow {
  size_t table;
  size_t position;
  std::vector<QByteArray> cells;
};
struct RemoveTableRow {
  size_t table;
  size_t index;
};
struct SetTableAlignment {
  size_t table;
  size_t column;
  TableAlignment alignment;
};
struct InsertTableColumn {
  size_t table;
  size_t position;
  QByteArray header;
  std::vector<QByteArray> bodyCells;
  TableAlignment alignment;
};
struct RemoveTableColumn {
  size_t table;
  size_t index;
};
struct RemoveTable {
  size_t table;
};
struct InsertTableCellText {
  size_t cell;
  QByteArray text;  // Nonempty single-line UTF-8 plain text.
};
struct InsertTable {
  size_t position;  // Root child position, 0..children.size().
  size_t columns;
  size_t bodyRows;
};
struct InsertHorizontalRule {
  size_t position;
};
struct InsertFencedCode {
  size_t position;
};

using EditCommand =
    std::variant<ReplaceText, SetTaskChecked, SetHeadingLevel, ToggleInlineStyle,
                 InsertListItem, RemoveListItem, SetListKind, SetItemTask,
                 WrapInLink, InsertImage, WrapInQuote, UnwrapQuote,
                 WrapInList, ReplaceCodeBody,
                 IndentListItem, OutdentListItem, InsertTableRow,
                 RemoveTableRow, SetTableAlignment,
                 InsertTableColumn, RemoveTableColumn, RemoveTable,
                 InsertTableCellText, InsertTable, InsertHorizontalRule,
                 InsertFencedCode>;

// Edits return a reparsed snapshot; no-op preserves identity, revision and
// bytes.
ParseResult applyEdit(const Document& document, const SnapshotToken& token,
                      const EditCommand& command);

}  // namespace markpad::markdown

#endif  // MARKPAD_SRC_MARKDOWN_DOCUMENT_H_
