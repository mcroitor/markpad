#ifndef MARKPAD_EXPERIMENTS_MD4C_SOURCE_DOCUMENT_H_
#define MARKPAD_EXPERIMENTS_MD4C_SOURCE_DOCUMENT_H_

#include <md4c.h>

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QUuid>
#include <optional>
#include <variant>
#include <vector>

namespace markpad::md4c_spike {

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
using EditCommand = std::variant<ReplaceText, SetTaskChecked, SetHeadingLevel>;

// Edits return a reparsed snapshot; no-op preserves identity, revision and
// bytes.
ParseResult applyEdit(const Document& document, const SnapshotToken& token,
                      const EditCommand& command);

}  // namespace markpad::md4c_spike

#endif  // MARKPAD_EXPERIMENTS_MD4C_SOURCE_DOCUMENT_H_
