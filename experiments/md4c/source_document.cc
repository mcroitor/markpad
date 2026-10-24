#include "source_document.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringDecoder>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace markpad::md4c_spike {
namespace {

constexpr unsigned kGfmFlags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH |
                               MD_FLAG_TASKLISTS | MD_FLAG_PERMISSIVEAUTOLINKS;

struct EditResult {
  std::optional<QByteArray> source;
  QString error;
};

QString attribute(const MD_ATTRIBUTE& value) {
  return QString::fromUtf8(value.text, value.size);
}

class Builder {
 public:
  Document document;
  std::vector<size_t> stack;
  QString error;

  int enter(NodeKind kind, int type, QJsonObject attributes = {},
            std::optional<SourceRange> range = std::nullopt,
            QByteArray text = {}) {
    const auto index = document.nodes.size();
    if (!stack.empty()) {
      document.nodes[stack.back()].children.push_back(index);
    }
    document.nodes.push_back(
        {kind,
         type,
         std::move(attributes),
         std::move(text),
         {},
         range,
         stack.empty() ? std::nullopt : std::optional<size_t>(stack.back())});
    if (kind != NodeKind::kText) {
      stack.push_back(index);
    }
    return 0;
  }

  int leave(NodeKind kind, int type) {
    if (stack.empty() || document.nodes[stack.back()].kind != kind ||
        document.nodes[stack.back()].type != type) {
      error = QStringLiteral("Unbalanced MD4C callbacks");
      return 1;
    }
    stack.pop_back();
    return 0;
  }

  static int enterBlock(MD_BLOCKTYPE type, void* detail, void* data) {
    auto& self = *static_cast<Builder*>(data);
    QJsonObject attributes;
    std::optional<SourceRange> range;
    switch (type) {
      case MD_BLOCK_H:
        attributes["level"] =
            int(static_cast<MD_BLOCK_H_DETAIL*>(detail)->level);
        break;
      case MD_BLOCK_UL:
        attributes["tight"] =
            bool(static_cast<MD_BLOCK_UL_DETAIL*>(detail)->is_tight);
        break;
      case MD_BLOCK_OL: {
        const auto& list = *static_cast<MD_BLOCK_OL_DETAIL*>(detail);
        attributes["start"] = qint64(list.start);
        attributes["tight"] = bool(list.is_tight);
        break;
      }
      case MD_BLOCK_LI: {
        const auto& item = *static_cast<MD_BLOCK_LI_DETAIL*>(detail);
        attributes["task"] = bool(item.is_task);
        if (item.is_task) {
          attributes["checked"] = item.task_mark != ' ';
          range = SourceRange{item.task_mark_offset, 1};
        }
        break;
      }
      case MD_BLOCK_CODE: {
        const auto& code = *static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
        attributes["info"] = attribute(code.info);
        attributes["lang"] = attribute(code.lang);
        break;
      }
      case MD_BLOCK_TABLE: {
        const auto& table = *static_cast<MD_BLOCK_TABLE_DETAIL*>(detail);
        attributes["columns"] = int(table.col_count);
        attributes["head_rows"] = int(table.head_row_count);
        attributes["body_rows"] = int(table.body_row_count);
        break;
      }
      case MD_BLOCK_TH:
      case MD_BLOCK_TD:
        attributes["align"] =
            int(static_cast<MD_BLOCK_TD_DETAIL*>(detail)->align);
        break;
      default:
        break;
    }
    return self.enter(NodeKind::kBlock, type, std::move(attributes), range);
  }

  static int enterSpan(MD_SPANTYPE type, void* detail, void* data) {
    QJsonObject attributes;
    if (type == MD_SPAN_A) {
      const auto& link = *static_cast<MD_SPAN_A_DETAIL*>(detail);
      attributes["href"] = attribute(link.href);
      attributes["title"] = attribute(link.title);
    } else if (type == MD_SPAN_IMG) {
      const auto& image = *static_cast<MD_SPAN_IMG_DETAIL*>(detail);
      attributes["src"] = attribute(image.src);
      attributes["title"] = attribute(image.title);
    }
    return static_cast<Builder*>(data)->enter(NodeKind::kSpan, type,
                                              std::move(attributes));
  }

  static int text(MD_TEXTTYPE type, const MD_CHAR* value, MD_SIZE size,
                  void* data) {
    auto& self = *static_cast<Builder*>(data);
    std::optional<SourceRange> range;
    // Some callbacks use synthesized buffers. Never infer a range by text
    // search.
    const auto base =
        reinterpret_cast<uintptr_t>(self.document.source.constData());
    const auto address = reinterpret_cast<uintptr_t>(value);
    const auto length = static_cast<uintptr_t>(self.document.source.size());
    if (address >= base && address - base <= length &&
        size <= length - (address - base)) {
      range = SourceRange{static_cast<qsizetype>(address - base),
                          static_cast<qsizetype>(size)};
    }
    return self.enter(NodeKind::kText, type, {}, range,
                      QByteArray(value, size));
  }
};

QJsonObject describe(const Document& document, size_t index) {
  const auto& node = document.nodes[index];
  QJsonArray children;
  QByteArray text;
  int textType = -1;
  auto flush = [&] {
    if (textType != -1) {
      children.append(QJsonObject{{"kind", int(NodeKind::kText)},
                                  {"type", textType},
                                  {"text", QString::fromUtf8(text)}});
      text.clear();
      textType = -1;
    }
  };
  for (auto child : node.children) {
    const auto& value = document.nodes[child];
    if (value.kind == NodeKind::kText) {
      if (textType != value.type) {
        flush();
        textType = value.type;
      }
      text += value.text;
    } else {
      flush();
      children.append(describe(document, child));
    }
  }
  flush();
  return {{"kind", int(node.kind)},
          {"type", node.type},
          {"attributes", node.attributes},
          {"children", children}};
}

bool validUtf8(const QByteArray& text) {
  QStringDecoder decoder(QStringDecoder::Utf8,
                         QStringConverter::Flag::Stateless);
  const QString decoded = decoder(text);
  Q_UNUSED(decoded);
  return !decoder.hasError();
}

EditResult apply(const Document& original, const Document& expected,
                 SourceRange range, const QByteArray& replacement) {
  if (range.offset < 0 || range.size < 0 ||
      range.offset > original.source.size() ||
      range.size > original.source.size() - range.offset) {
    return {std::nullopt, QStringLiteral("Invalid source range")};
  }
  auto changed = original.source;
  changed.replace(range.offset, range.size, replacement);
  auto reparsed = parse(changed);
  if (!reparsed.document) {
    return {std::nullopt, reparsed.error};
  }
  if (signature(*reparsed.document) != signature(expected)) {
    return {std::nullopt,
            QStringLiteral("Edit changes surrounding Markdown semantics; "
                           "block serializer is required")};
  }
  return {std::move(changed), {}};
}

}  // namespace

ParseResult parse(QByteArray source, quint64 revision) {
  if (static_cast<quint64>(source.size()) >
          std::numeric_limits<MD_SIZE>::max() ||
      !validUtf8(source)) {
    return {std::nullopt,
            QStringLiteral("Input must be UTF-8 within MD4C size limit")};
  }
  Builder builder;
  builder.document.source = std::move(source);
  builder.document.revision = revision;
  builder.document.identity = QUuid::createUuid();
  MD_PARSER parser{};
  parser.flags = kGfmFlags;
  parser.enter_block = Builder::enterBlock;
  parser.leave_block = [](MD_BLOCKTYPE type, void*, void* data) {
    return static_cast<Builder*>(data)->leave(NodeKind::kBlock, type);
  };
  parser.enter_span = Builder::enterSpan;
  parser.leave_span = [](MD_SPANTYPE type, void*, void* data) {
    return static_cast<Builder*>(data)->leave(NodeKind::kSpan, type);
  };
  parser.text = Builder::text;
  parser.debug_log = [](const char* message, void* data) {
    static_cast<Builder*>(data)->error = QString::fromUtf8(message);
  };
  const auto status = md_parse(
      builder.document.source.constData(),
      static_cast<MD_SIZE>(builder.document.source.size()), &parser, &builder);
  if (status != 0 || !builder.error.isEmpty() || !builder.stack.empty() ||
      builder.document.nodes.empty()) {
    return {std::nullopt, builder.error.isEmpty()
                              ? QStringLiteral("MD4C parse failed")
                              : builder.error};
  }
  return {std::move(builder.document), {}};
}

QByteArray signature(const Document& document) {
  return QJsonDocument(describe(document, 0)).toJson(QJsonDocument::Compact);
}

namespace {

EditResult replaceTextSource(const Document& document, size_t index,
                             const QByteArray& text) {
  if (index >= document.nodes.size()) {
    return {std::nullopt, QStringLiteral("Invalid node index")};
  }
  const auto& node = document.nodes[index];
  if (node.kind != NodeKind::kText || node.type != MD_TEXT_NORMAL ||
      !node.source || node.source->size != node.text.size() ||
      node.source->offset < 0 || node.source->offset > document.source.size() ||
      node.source->size > document.source.size() - node.source->offset ||
      document.source.mid(node.source->offset, node.source->size) !=
          node.text) {
    return {std::nullopt,
            QStringLiteral("Only source-mapped normal text is editable")};
  }
  if (text.isEmpty() || text.contains('\n') || text.contains('\r') ||
      text.contains('\0') || !validUtf8(text)) {
    return {
        std::nullopt,
        QStringLiteral("This spike requires nonempty single-line UTF-8 text")};
  }
  if (text == node.text) {
    return {document.source, {}};
  }
  QByteArray escaped;
  constexpr auto kPunctuation = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
  for (auto character : text) {
    if (QByteArrayView(kPunctuation).contains(character)) {
      escaped += '\\';
    }
    escaped += character;
  }
  auto expected = document;
  expected.nodes[index].text = text;
  return apply(document, expected, *node.source, escaped);
}

EditResult setTaskCheckedSource(const Document& document, size_t index,
                                bool checked) {
  if (index >= document.nodes.size()) {
    return {std::nullopt, QStringLiteral("Invalid node index")};
  }
  const auto& node = document.nodes[index];
  if (node.kind != NodeKind::kBlock || node.type != MD_BLOCK_LI ||
      !node.attributes["task"].toBool() || !node.source ||
      node.source->size != 1 || node.source->offset < 1 ||
      node.source->offset >= document.source.size() - 1 ||
      document.source[node.source->offset - 1] != '[' ||
      document.source[node.source->offset + 1] != ']') {
    return {std::nullopt, QStringLiteral("Node is not a mapped task marker")};
  }
  const auto marker = document.source[node.source->offset];
  if ((marker != ' ' && marker != 'x' && marker != 'X') ||
      (marker != ' ') != node.attributes["checked"].toBool()) {
    return {std::nullopt,
            QStringLiteral("Task marker does not match the model")};
  }
  if (node.attributes["checked"].toBool() == checked) {
    return {document.source, {}};
  }
  auto expected = document;
  expected.nodes[index].attributes["checked"] = checked;
  return apply(document, expected, *node.source, checked ? "x" : " ");
}

bool textBounds(const Document& document, size_t index, qsizetype& first,
                qsizetype& end) {
  const auto& node = document.nodes[index];
  if (node.kind == NodeKind::kText) {
    if (!node.source || node.source->offset < 0 || node.source->size <= 0 ||
        node.source->offset > document.source.size() ||
        node.source->size > document.source.size() - node.source->offset) {
      return false;
    }
    first = qMin(first, node.source->offset);
    end = qMax(end, node.source->offset + node.source->size);
  }
  for (auto child : node.children) {
    if (!textBounds(document, child, first, end)) {
      return false;
    }
  }
  return true;
}

bool space(char character) { return character == ' ' || character == '\t'; }

std::optional<SourceRange> collectEvidence(
    const Document& document, size_t index,
    std::vector<EvidenceEnvelope>& envelopes) {
  const auto& node = document.nodes[index];
  qsizetype first = document.source.size();
  qsizetype end = 0;
  auto include = [&](SourceRange range) {
    if (range.offset >= 0 && range.size > 0 &&
        range.offset <= document.source.size() &&
        range.size <= document.source.size() - range.offset) {
      first = qMin(first, range.offset);
      end = qMax(end, range.offset + range.size);
    }
  };
  if (node.source) {
    include(*node.source);
  }
  for (auto child : node.children) {
    auto evidence = collectEvidence(document, child, envelopes);
    if (evidence) {
      include(*evidence);
    }
  }
  if (first >= end) {
    return std::nullopt;
  }
  SourceRange range{first, end - first};
  if (node.kind == NodeKind::kBlock) {
    envelopes.push_back({index, node.parent, range});
  }
  return range;
}

}  // namespace

std::vector<EvidenceEnvelope> evidenceEnvelopes(const Document& document) {
  std::vector<EvidenceEnvelope> result;
  if (!document.nodes.empty()) {
    collectEvidence(document, 0, result);
  }
  return result;
}

std::vector<BlockSource> mapBlocks(const Document& document) {
  std::vector<BlockSource> result;
  if (document.nodes.empty()) {
    return result;
  }
  for (size_t index = 1; index < document.nodes.size(); ++index) {
    const auto& node = document.nodes[index];
    if (node.kind != NodeKind::kBlock ||
        (node.type != MD_BLOCK_P && node.type != MD_BLOCK_H)) {
      continue;
    }
    size_t quoteDepth = 0;
    auto ancestor = node.parent;
    bool supported = true;
    while (ancestor && *ancestor != 0) {
      const auto& container = document.nodes[*ancestor];
      if (container.kind != NodeKind::kBlock ||
          container.type != MD_BLOCK_QUOTE) {
        supported = false;
        break;
      }
      ++quoteDepth;
      ancestor = container.parent;
    }
    if (!supported) {
      continue;
    }
    qsizetype first = document.source.size();
    qsizetype end = 0;
    if (!textBounds(document, index, first, end) || first >= end) {
      continue;
    }
    const auto start =
        first == 0 ? 0 : document.source.lastIndexOf('\n', first - 1) + 1;
    auto lineEnd = document.source.indexOf('\n', start);
    if (lineEnd < 0) {
      lineEnd = document.source.size();
    }
    auto bodyEnd = lineEnd;
    if (bodyEnd > start && document.source[bodyEnd - 1] == '\r') {
      --bodyEnd;
    }
    if (end > bodyEnd) {
      continue;
    }
    const auto fullEnd =
        lineEnd < document.source.size() ? lineEnd + 1 : lineEnd;
    auto blockStart = start;
    for (size_t depth = 0; depth < quoteDepth; ++depth) {
      const auto indentStart = blockStart;
      while (blockStart < bodyEnd && document.source[blockStart] == ' ') {
        ++blockStart;
      }
      if (blockStart - indentStart > 3 || blockStart == bodyEnd ||
          document.source[blockStart] != '>') {
        supported = false;
        break;
      }
      ++blockStart;
      if (blockStart < bodyEnd && document.source[blockStart] == ' ') {
        ++blockStart;
      }
    }
    if (!supported) {
      continue;
    }
    const auto candidate =
        document.source.mid(blockStart, fullEnd - blockStart);
    auto isolated = parse(candidate);
    if (!isolated.document ||
        isolated.document->nodes[0].children.size() != 1 ||
        describe(*isolated.document, isolated.document->nodes[0].children[0]) !=
            describe(document, index)) {
      continue;
    }
    auto contentStart = blockStart;
    while (contentStart < bodyEnd && document.source[contentStart] == ' ') {
      ++contentStart;
    }
    if (contentStart - blockStart > 3) {
      continue;
    }
    auto contentEnd = bodyEnd;
    if (node.type == MD_BLOCK_H) {
      const auto markerStart = contentStart;
      while (contentStart < bodyEnd && document.source[contentStart] == '#') {
        ++contentStart;
      }
      if (contentStart - markerStart != node.attributes["level"].toInt() ||
          contentStart == bodyEnd || !space(document.source[contentStart])) {
        continue;
      }
      while (contentStart < bodyEnd && space(document.source[contentStart])) {
        ++contentStart;
      }
      while (contentEnd > contentStart &&
             space(document.source[contentEnd - 1])) {
        --contentEnd;
      }
      auto hashes = contentEnd;
      while (hashes > contentStart && document.source[hashes - 1] == '#') {
        --hashes;
      }
      if (hashes < contentEnd && hashes > contentStart &&
          space(document.source[hashes - 1])) {
        contentEnd = hashes;
        while (contentEnd > contentStart &&
               space(document.source[contentEnd - 1])) {
          --contentEnd;
        }
      }
    }
    if (contentStart >= contentEnd) {
      continue;
    }
    result.push_back({index,
                      {blockStart, fullEnd - blockStart},
                      {contentStart, contentEnd - contentStart}});
  }
  return result;
}

SnapshotToken snapshotToken(const Document& document) {
  return {document.revision,
          QCryptographicHash::hash(document.source, QCryptographicHash::Sha256),
          document.identity};
}

namespace {

EditResult setHeadingLevelSource(const Document& document, size_t index,
                                 int level) {
  if (level < 0 || level > 6 || index >= document.nodes.size()) {
    return {std::nullopt,
            QStringLiteral("Invalid heading level or node index")};
  }
  const auto blocks = mapBlocks(document);
  const BlockSource* mapped = nullptr;
  for (const auto& block : blocks) {
    if (block.node == index) {
      mapped = &block;
      break;
    }
  }
  if (!mapped) {
    return {std::nullopt,
            QStringLiteral("Block has no supported source range")};
  }
  const auto& node = document.nodes[index];
  const auto oldLevel =
      node.type == MD_BLOCK_H ? node.attributes["level"].toInt() : 0;
  if (level == oldLevel) {
    return {document.source, {}};
  }
  const auto original =
      document.source.mid(mapped->range.offset, mapped->range.size);
  QByteArray ending;
  if (original.endsWith("\r\n")) {
    ending = "\r\n";
  } else if (original.endsWith('\n')) {
    ending = "\n";
  } else if (original.endsWith('\r')) {
    ending = "\r";
  }
  auto replacement =
      document.source.mid(mapped->content.offset, mapped->content.size);
  if (level > 0) {
    replacement.prepend(QByteArray(level, '#') + ' ');
  }
  replacement += ending;
  auto expected = document;
  expected.nodes[index].type = level == 0 ? MD_BLOCK_P : MD_BLOCK_H;
  expected.nodes[index].attributes = {};
  if (level > 0) {
    expected.nodes[index].attributes["level"] = level;
  }
  return apply(document, expected, mapped->range, replacement);
}

}  // namespace

ParseResult applyEdit(const Document& document, const SnapshotToken& token,
                      const EditCommand& command) {
  const auto current = snapshotToken(document);
  if (document.identity.isNull() || token.identity != current.identity ||
      token.revision != current.revision || token.digest != current.digest) {
    return {std::nullopt, QStringLiteral("Stale or foreign document snapshot")};
  }
  auto edited = std::visit(
      [&](const auto& edit) -> EditResult {
        using Command = std::decay_t<decltype(edit)>;
        if constexpr (std::is_same_v<Command, ReplaceText>) {
          return replaceTextSource(document, edit.node, edit.text);
        } else if constexpr (std::is_same_v<Command, SetTaskChecked>) {
          return setTaskCheckedSource(document, edit.node, edit.checked);
        } else {
          return setHeadingLevelSource(document, edit.node, edit.level);
        }
      },
      command);
  if (!edited.source) {
    return {std::nullopt, edited.error};
  }
  if (*edited.source == document.source) {
    return {document, {}};
  }
  if (document.revision == std::numeric_limits<quint64>::max()) {
    return {std::nullopt, QStringLiteral("Document revision overflow")};
  }
  auto result = parse(std::move(*edited.source), document.revision + 1);
  if (result.document) {
    result.document->identity = document.identity;
  }
  return result;
}

}  // namespace markpad::md4c_spike
