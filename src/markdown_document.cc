#include "markdown_document.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringDecoder>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace markpad::markdown {
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

QJsonArray attributeSegments(const MD_ATTRIBUTE& value) {
  QJsonArray segments;
  if (value.size == 0) {
    return segments;
  }
  for (MD_SIZE index = 0; value.substr_offsets[index] < value.size; ++index) {
    const auto start = value.substr_offsets[index];
    const auto size = value.substr_offsets[index + 1] - start;
    segments.append(
        QJsonObject{{"type", int(value.substr_types[index])},
                    {"text", QString::fromUtf8(value.text + start, size)}});
  }
  return segments;
}

void addAttribute(QJsonObject& attributes, const char* name,
                  const MD_ATTRIBUTE& value) {
  attributes[name] = attribute(value);
  attributes[QString::fromLatin1(name) + QStringLiteral("_segments")] =
      attributeSegments(value);
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
        addAttribute(attributes, "info", code.info);
        addAttribute(attributes, "lang", code.lang);
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
      addAttribute(attributes, "href", link.href);
      addAttribute(attributes, "title", link.title);
      attributes["autolink"] = bool(link.is_autolink);
    } else if (type == MD_SPAN_IMG) {
      const auto& image = *static_cast<MD_SPAN_IMG_DETAIL*>(detail);
      addAttribute(attributes, "src", image.src);
      addAttribute(attributes, "title", image.title);
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
    return {std::nullopt,
            QStringLiteral(
                "Text replacement requires nonempty single-line UTF-8 text")};
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

bool mappedTextBounds(const Document& document, size_t index,
                      qsizetype& first, qsizetype& end) {
  const auto& node = document.nodes[index];
  if (node.kind == NodeKind::kText) {
    if (!node.source) {
      return true;
    }
    if (node.source->offset < 0 || node.source->size < 0 ||
        node.source->offset > document.source.size() ||
        node.source->size > document.source.size() - node.source->offset) {
      return false;
    }
    if (node.source->size > 0) {
      first = qMin(first, node.source->offset);
      end = qMax(end, node.source->offset + node.source->size);
    }
  }
  for (auto child : node.children) {
    if (!mappedTextBounds(document, child, first, end)) {
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

namespace {

struct ListLine {
  qsizetype start;
  qsizetype bodyEnd;
  qsizetype end;
};

std::vector<ListLine> listLines(const QByteArray& source) {
  std::vector<ListLine> lines;
  qsizetype start = 0;
  while (start < source.size()) {
    auto end = source.indexOf('\n', start);
    if (end < 0) {
      end = source.size();
    }
    auto bodyEnd = end;
    if (bodyEnd > start && source[bodyEnd - 1] == '\r') {
      --bodyEnd;
    }
    lines.push_back({start, bodyEnd, end < source.size() ? end + 1 : end});
    start = lines.back().end;
  }
  return lines;
}

struct ListMarker {
  qsizetype indent;
  qsizetype content;
  bool ordered;
  char delimiter;
};

std::optional<ListMarker> listMarker(const QByteArray& source,
                                     const ListLine& line) {
  auto position = line.start;
  while (position < line.bodyEnd && source[position] == ' ') {
    ++position;
  }
  const auto indent = position - line.start;
  if (indent > 3 || position == line.bodyEnd) {
    return std::nullopt;
  }
  const auto digits = position;
  while (position < line.bodyEnd && source[position] >= '0' &&
         source[position] <= '9') {
    ++position;
  }
  const bool ordered = position > digits;
  if (position == line.bodyEnd || position - digits > 9) {
    return std::nullopt;
  }
  const char delimiter = source[position++];
  if ((ordered && delimiter != '.' && delimiter != ')') ||
      (!ordered && delimiter != '-' && delimiter != '+' && delimiter != '*') ||
      position == line.bodyEnd || source[position++] != ' ' ||
      (position < line.bodyEnd && space(source[position]))) {
    return std::nullopt;
  }
  return ListMarker{indent, position, ordered, delimiter};
}

bool sameMarker(const ListMarker& left, const ListMarker& right) {
  return left.indent == right.indent && left.ordered == right.ordered &&
         left.delimiter == right.delimiter;
}

struct ListFence {
  char marker = 0;
  qsizetype count = 0;
};

void updateListFence(const QByteArray& source, qsizetype start, qsizetype end,
                     ListFence& fence) {
  const auto initial = start;
  while (start < end && source[start] == ' ') {
    ++start;
  }
  if (start - initial > 3 || start == end) {
    return;
  }
  const auto marker = source[start];
  if (marker != '`' && marker != '~') {
    return;
  }
  auto position = start;
  while (position < end && source[position] == marker) {
    ++position;
  }
  const auto count = position - start;
  if (fence.marker) {
    if (marker != fence.marker || count < fence.count) {
      return;
    }
    while (position < end && source[position] == ' ') {
      ++position;
    }
    if (position == end) {
      fence = {};
    }
  } else if (count >= 3 &&
             (marker != '`' ||
              !source.mid(position, end - position).contains('`'))) {
    fence = {marker, count};
  }
}

std::optional<ListSource> scanList(const QByteArray& source,
                                   const std::vector<ListLine>& lines,
                                   size_t first) {
  const auto marker = listMarker(source, lines[first]);
  if (!marker) {
    return std::nullopt;
  }
  ListSource mapped{0, {lines[first].start, 0}, {}, {}, {}};
  ListFence fence;
  qsizetype indent = 0;
  size_t contentCount = 0;
  for (size_t i = first; i < lines.size(); ++i) {
    const auto& line = lines[i];
    auto content = line.start;
    while (content < line.bodyEnd && source[content] == ' ') {
      ++content;
    }
    const auto leading = content;
    const bool blank = content == line.bodyEnd;
    const auto next = listMarker(source, line);
    if (!fence.marker && next && sameMarker(*marker, *next)) {
      if (!mapped.items.empty()) {
        mapped.items.back().contentLines.resize(contentCount);
      }
      mapped.items.push_back({0, {line.start, 0}, {}});
      contentCount = 0;
      content = next->content;
      indent = content - line.start;
    } else {
      if (!blank && content - line.start < indent) {
        break;
      }
      content = std::min(line.start + indent, line.bodyEnd);
    }
    if (mapped.items.empty() ||
        (!fence.marker && leading < line.bodyEnd && source[leading] == '\t')) {
      return std::nullopt;
    }
    // Indented code has a different ownership contract and stays unsupported.
    auto body = source.mid(content, line.bodyEnd - content);
    if (!fence.marker && !blank) {
      if (body.startsWith("    ") || body.contains('\r')) {
        return std::nullopt;
      }
    }
    updateListFence(source, content, line.bodyEnd, fence);
    auto& item = mapped.items.back();
    item.contentLines.push_back({content, line.end - content});
    if (!blank || fence.marker) {
      item.range.size = line.end - item.range.offset;
      contentCount = item.contentLines.size();
      mapped.range.size = line.end - mapped.range.offset;
    }
  }
  if (fence.marker || mapped.items.empty()) {
    return std::nullopt;
  }
  mapped.items.back().contentLines.resize(contentCount);
  return mapped;
}

std::optional<qsizetype> quotePrefixEnd(const QByteArray& source,
                                        const ListLine& line) {
  auto position = line.start;
  while (position < line.bodyEnd && source[position] == ' ') {
    ++position;
  }
  if (position - line.start > 3 || position >= line.bodyEnd ||
      source[position] != '>') {
    return std::nullopt;
  }
  ++position;
  if (position < line.bodyEnd && source[position] == ' ') {
    ++position;
  }
  return position;
}

bool blankListLine(const QByteArray& source, const ListLine& line) {
  for (auto position = line.start; position < line.bodyEnd; ++position) {
    if (source[position] != ' ' && source[position] != '\t') {
      return false;
    }
  }
  return true;
}

std::optional<SourceRange> scanQuote(const QByteArray& source,
                                     const std::vector<ListLine>& lines,
                                     size_t first) {
  if (first >= lines.size() || !quotePrefixEnd(source, lines[first])) {
    return std::nullopt;
  }
  auto end = lines[first].end;
  for (size_t i = first + 1; i < lines.size(); ++i) {
    const auto& line = lines[i];
    if (quotePrefixEnd(source, line)) {
      end = line.end;
      continue;
    }
    if (blankListLine(source, line)) {
      break;
    }
    // A non-blank line without an explicit prefix would be a lazy
    // continuation. That ownership is not proven by this slice.
    return std::nullopt;
  }
  return SourceRange{lines[first].start, end - lines[first].start};
}

bool sameListSources(const Document& original, size_t index,
                     const Document& isolated, size_t local, qsizetype offset,
                     bool& anchored) {
  const auto& node = original.nodes[index];
  const auto& other = isolated.nodes[local];
  if (node.kind != other.kind || node.type != other.type ||
      node.attributes != other.attributes || node.text != other.text ||
      node.children.size() != other.children.size() ||
      node.source.has_value() != other.source.has_value()) {
    return false;
  }
  if (node.source) {
    if (node.source->offset != other.source->offset + offset ||
        node.source->size != other.source->size) {
      return false;
    }
    anchored = true;
  }
  for (size_t i = 0; i < node.children.size(); ++i) {
    if (!sameListSources(original, node.children[i], isolated,
                         other.children[i], offset, anchored)) {
      return false;
    }
  }
  return true;
}

bool supportedListChildren(const Document& document, size_t index) {
  const auto& node = document.nodes[index];
  for (auto child : node.children) {
    const auto& value = document.nodes[child];
    if (value.kind == NodeKind::kBlock && value.type == MD_BLOCK_TABLE) {
      continue;
    }
    if (value.kind == NodeKind::kBlock && value.type != MD_BLOCK_LI &&
        value.type != MD_BLOCK_P && value.type != MD_BLOCK_CODE &&
        value.type != MD_BLOCK_UL && value.type != MD_BLOCK_OL &&
        value.type != MD_BLOCK_QUOTE) {
      return false;
    }
    if (!supportedListChildren(document, child)) {
      return false;
    }
  }
  return true;
}

struct DefinitionCandidate {
  SourceRange range;  // Full syntax including the final EOL.
  SourceRange label;
  QByteArray text;  // Raw label bytes.
  qsizetype begin = 0;
  bool plain = false;
};

std::optional<DefinitionCandidate> definitionAt(const QByteArray& source,
                                                const std::vector<ListLine>& lines,
                                                size_t first, qsizetype begin) {
  if (first >= lines.size() || begin < lines[first].start ||
      begin >= lines[first].bodyEnd || source[begin] != '[') {
    return std::nullopt;
  }
  auto lineIndex = first;
  auto position = begin + 1;
  auto labelBegin = position;
  qsizetype labelSize = 0;
  bool labelClosed = false;
  while (lineIndex < lines.size() && !labelClosed) {
    const auto& line = lines[lineIndex];
    while (position < line.bodyEnd) {
      const auto character = source[position];
      if (character == '\\' && position + 1 < line.bodyEnd) {
        position += 2;
        labelSize += 2;
      } else if (character == '[') {
        return std::nullopt;
      } else if (character == ']') {
        if (position + 1 >= line.bodyEnd || source[position + 1] != ':') {
          return std::nullopt;
        }
        labelClosed = true;
        position += 2;
        break;
      } else {
        ++position;
        ++labelSize;
      }
      if (labelSize > 999) {
        return std::nullopt;
      }
    }
    if (labelClosed) {
      break;
    }
    ++lineIndex;
    if (lineIndex >= lines.size() ||
        lines[lineIndex].bodyEnd == lines[lineIndex].start) {
      return std::nullopt;
    }
    position = lines[lineIndex].start;
    while (position < lines[lineIndex].bodyEnd &&
           (source[position] == ' ' || source[position] == '\t')) {
      ++position;
    }
    if (position == lines[lineIndex].bodyEnd) {
      return std::nullopt;
    }
  }
  if (!labelClosed) {
    return std::nullopt;
  }
  const auto labelEnd = position - 2;
  if (labelEnd <= labelBegin) {
    return std::nullopt;
  }

  // Optional whitespace with at most one line break before the destination.
  while (position < lines[lineIndex].bodyEnd && space(source[position])) {
    ++position;
  }
  if (position >= lines[lineIndex].bodyEnd) {
    ++lineIndex;
    if (lineIndex >= lines.size()) {
      return std::nullopt;
    }
    position = lines[lineIndex].start;
    auto indent = position;
    while (position < lines[lineIndex].bodyEnd && source[position] == ' ') {
      ++position;
    }
    if (position - indent > 3) {
      return std::nullopt;
    }
  }

  const auto destinationLine = lineIndex;
  const auto destination = position;
  if (position >= lines[lineIndex].bodyEnd) {
    return std::nullopt;
  }
  if (source[position] == '<') {
    ++position;
    bool closed = false;
    while (position < lines[lineIndex].bodyEnd) {
      const auto character = source[position];
      if (character == '\\' && position + 1 < lines[lineIndex].bodyEnd) {
        position += 2;
      } else if (character == '<' || character == '\r') {
        return std::nullopt;
      } else if (character == '>') {
        ++position;
        closed = true;
        break;
      } else {
        ++position;
      }
    }
    if (!closed) {
      return std::nullopt;
    }
  } else {
    qsizetype depth = 0;
    while (position < lines[lineIndex].bodyEnd) {
      const auto character = source[position];
      if (character == '\\' && position + 1 < lines[lineIndex].bodyEnd) {
        position += 2;
        continue;
      }
      if (space(character) || character == '\r') {
        break;
      }
      if (character == '(') {
        if (++depth > 32) {
          return std::nullopt;
        }
      } else if (character == ')') {
        if (depth == 0) {
          break;
        }
        --depth;
      }
      ++position;
    }
    if (position == destination || depth != 0) {
      return std::nullopt;
    }
  }
  if (lineIndex == destinationLine && position < lines[lineIndex].bodyEnd &&
      !space(source[position])) {
    return std::nullopt;
  }

  // Optional title, either on the destination line or the immediately
  // following one.
  bool titleSeen = false;
  auto titleLine = lineIndex;
  auto titlePosition = position;
  const auto destinationEnd = position;
  while (titlePosition < lines[titleLine].bodyEnd &&
         space(source[titlePosition])) {
    ++titlePosition;
  }
  if (titlePosition >= lines[titleLine].bodyEnd) {
    if (titleLine + 1 < lines.size() &&
        lines[titleLine + 1].bodyEnd > lines[titleLine + 1].start) {
      auto next = lines[titleLine + 1].start;
      auto nextIndent = next;
      while (next < lines[titleLine + 1].bodyEnd && source[next] == ' ') {
        ++next;
      }
      if (next - nextIndent <= 3 &&
          (source[next] == '"' || source[next] == '\'' ||
           source[next] == '(')) {
        titleLine += 1;
        titlePosition = next;
      }
    }
  }
  if (titlePosition < lines[titleLine].bodyEnd &&
      (source[titlePosition] == '"' || source[titlePosition] == '\'' ||
       source[titlePosition] == '(') &&
      (titleLine != lineIndex || titlePosition > destinationEnd)) {
    const auto closer = source[titlePosition] == '(' ? ')' : source[titlePosition];
    ++titlePosition;
    bool closed = false;
    while (titleLine < lines.size() && !closed) {
      const auto& line = lines[titleLine];
      while (titlePosition < line.bodyEnd) {
        const auto character = source[titlePosition];
        if (character == '\\' && titlePosition + 1 < line.bodyEnd) {
          titlePosition += 2;
        } else if (character == closer) {
          ++titlePosition;
          closed = true;
          break;
        } else if (closer == ')' && character == '(') {
          return std::nullopt;
        } else {
          ++titlePosition;
        }
      }
      if (!closed) {
        ++titleLine;
        if (titleLine >= lines.size()) {
          return std::nullopt;
        }
        titlePosition = lines[titleLine].start;
      }
    }
    const auto& line = lines[titleLine];
    while (titlePosition < line.bodyEnd && space(source[titlePosition])) {
      ++titlePosition;
    }
    if (titlePosition != line.bodyEnd) {
      return std::nullopt;
    }
    titleSeen = true;
  }

  if (!titleSeen) {
    const auto& line = lines[lineIndex];
    while (position < line.bodyEnd && space(source[position])) {
      ++position;
    }
    if (position != line.bodyEnd) {
      return std::nullopt;
    }
  }

  const auto lastLine = titleSeen ? titleLine : lineIndex;
  const auto end = lines[lastLine].end;
  return DefinitionCandidate{
      {lines[first].start, end - lines[first].start},
      {labelBegin, labelEnd - labelBegin},
      source.mid(labelBegin, labelEnd - labelBegin),
      begin,
      false};
}

bool textCovers(const Document& document, SourceRange range) {
  for (const auto& node : document.nodes) {
    if (node.kind != NodeKind::kText || !node.source || node.source->size <= 0) {
      continue;
    }
    const auto start = node.source->offset;
    const auto end = start + node.source->size;
    if (start < range.offset + range.size && range.offset < end) {
      return true;
    }
  }
  return false;
}

std::vector<DefinitionCandidate> definitionInventory(const Document& document) {
  std::vector<DefinitionCandidate> result;
  if (document.source.isEmpty()) {
    return result;
  }
  const auto lines = listLines(document.source);
  std::vector<qsizetype> starts;
  for (size_t i = 0; i < lines.size(); ++i) {
    const auto& line = lines[i];
    for (qsizetype indent = 0; indent <= 3 && indent < line.bodyEnd - line.start;
         ++indent) {
      starts.push_back(line.start + indent);
    }
    if (auto marker = listMarker(document.source, line)) {
      starts.push_back(marker->content);
    }
    auto quote = line.start;
    while (quote < line.bodyEnd && document.source[quote] == ' ') {
      ++quote;
    }
    if (quote - line.start <= 3 && quote < line.bodyEnd &&
        document.source[quote] == '>') {
      ++quote;
      if (quote < line.bodyEnd && document.source[quote] == ' ') {
        ++quote;
      }
      if (auto marker = listMarker(document.source, {quote, line.bodyEnd,
                                                     line.end})) {
        starts.push_back(marker->content);
      }
      starts.push_back(quote);
    }
  }
  std::sort(starts.begin(), starts.end());
  starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
  for (auto begin : starts) {
    const auto line = std::upper_bound(
        lines.begin(), lines.end(), begin,
        [](qsizetype value, const ListLine& entry) { return value < entry.start; });
    if (line == lines.begin()) {
      continue;
    }
    const auto index = static_cast<size_t>(line - lines.begin()) - 1;
    auto candidate = definitionAt(document.source, lines, index, begin);
    if (!candidate) {
      continue;
    }
    const bool duplicate = std::any_of(
        result.begin(), result.end(), [&candidate](const DefinitionCandidate& value) {
          return value.range.offset == candidate->range.offset;
        });
    if (duplicate) {
      continue;
    }
    const auto isolated = parse(
        document.source.mid(begin, candidate->range.offset +
                                       candidate->range.size - begin));
    if (!isolated.document ||
        !isolated.document->nodes[0].children.empty()) {
      continue;
    }
    if (textCovers(document, candidate->range)) {
      continue;
    }
    const auto& firstLine = lines[index];
    bool onlySpaces = begin >= firstLine.start && begin <= firstLine.start + 3;
    for (auto position = firstLine.start; position < begin && onlySpaces;
         ++position) {
      onlySpaces = document.source[position] == ' ';
    }
    candidate->plain = onlySpaces;
    result.push_back(*candidate);
  }
  std::sort(result.begin(), result.end(),
            [](const DefinitionCandidate& left, const DefinitionCandidate& right) {
              return left.range.offset < right.range.offset;
            });
  return result;
}

std::optional<bool> labelsEquivalent(const QByteArray& left,
                                     const QByteArray& right) {
  if (left == right) {
    return true;
  }
  const QByteArray probe =
      "[probe][" + left + "]\n\n[" + right + "]: /markpad-probe\n";
  const auto parsed = parse(probe);
  if (!parsed.document) {
    return std::nullopt;
  }
  for (const auto& node : parsed.document->nodes) {
    if (node.kind == NodeKind::kSpan && node.type == MD_SPAN_A &&
        node.attributes["href"].toString() == QStringLiteral("/markpad-probe")) {
      return true;
    }
  }
  return false;
}

QByteArray documentEol(const QByteArray& source) {
  const auto newline = source.indexOf('\n');
  if (newline < 0) {
    return source.contains('\r') ? QByteArray("\r") : QByteArray("\n");
  }
  if (newline > 0 && source[newline - 1] == '\r') {
    return QByteArray("\r\n");
  }
  return QByteArray("\n");
}

QByteArray normalizeEol(QByteArray text, const QByteArray& eol) {
  text.replace("\r\n", "\n");
  text.replace('\r', '\n');
  if (eol != "\n") {
    text.replace("\n", eol);
  }
  return text;
}

}  // namespace

namespace {

ListMapResult mapTopLevelList(const Document& document, size_t index) {
  if (index >= document.nodes.size() ||
      document.nodes[index].kind != NodeKind::kBlock ||
      (document.nodes[index].type != MD_BLOCK_UL &&
       document.nodes[index].type != MD_BLOCK_OL)) {
    return {std::nullopt, QStringLiteral("Invalid list target")};
  }
  const auto& node = document.nodes[index];
  if (node.parent != std::optional<size_t>(0) ||
      !supportedListChildren(document, index)) {
    return {std::nullopt,
            QStringLiteral("Unsupported list context: top-level lists without "
                           "nested containers are required")};
  }
  const auto lines = listLines(document.source);
  const auto evidence = evidenceEnvelopes(document);
  const auto definitions = definitionInventory(document);
  for (size_t first = 0; first < lines.size(); ++first) {
    auto mapped = scanList(document.source, lines, first);
    if (!mapped || mapped->items.size() != node.children.size()) {
      continue;
    }
    const auto listStart = mapped->range.offset;
    const auto listEnd = listStart + mapped->range.size;
    QByteArray before;
    QByteArray after;
    for (const auto& definition : definitions) {
      if (!definition.plain) {
        continue;
      }
      if (definition.range.offset + definition.range.size <= listStart) {
        before += document.source.mid(definition.range.offset,
                                      definition.range.size);
        if (!before.endsWith('\n') && !before.endsWith('\r')) {
          before += documentEol(document.source);
        }
      } else if (definition.range.offset >= listEnd) {
        after += document.source.mid(definition.range.offset,
                                     definition.range.size);
        if (!after.endsWith('\n') && !after.endsWith('\r')) {
          after += documentEol(document.source);
        }
      }
    }
    QByteArray context;
    if (!before.isEmpty()) {
      context += before;
      if (!context.endsWith("\n\n") && !context.endsWith("\r\n\r\n")) {
        context += documentEol(document.source);
      }
    }
    const auto fragmentOffset = context.size();
    context += document.source.mid(listStart, mapped->range.size);
    if (!after.isEmpty()) {
      while (!context.endsWith("\n\n") && !context.endsWith("\r\n\r\n")) {
        context += documentEol(document.source);
      }
      context += after;
    }
    auto isolated = parse(context);
    if (!isolated.document) {
      continue;
    }
    size_t isolatedList = isolated.document->nodes.size();
    for (auto child : isolated.document->nodes[0].children) {
      const auto& value = isolated.document->nodes[child];
      if (value.kind == NodeKind::kBlock &&
          (value.type == MD_BLOCK_UL || value.type == MD_BLOCK_OL)) {
        isolatedList = child;
        break;
      }
    }
    if (isolatedList >= isolated.document->nodes.size()) {
      continue;
    }
    bool anchored = false;
    if (!sameListSources(document, index, *isolated.document, isolatedList,
                         listStart - fragmentOffset, anchored) ||
        !anchored) {
      continue;
    }
    mapped->node = index;
    bool covered = true;
    for (size_t i = 0; i < mapped->items.size(); ++i) {
      auto& item = mapped->items[i];
      item.node = node.children[i];
      const auto envelope =
          std::find_if(evidence.begin(), evidence.end(),
                       [&item](const EvidenceEnvelope& value) {
                         return value.node == item.node;
                       });
      if (envelope != evidence.end() &&
          (envelope->range.offset < item.range.offset ||
           envelope->range.offset + envelope->range.size >
               item.range.offset + item.range.size)) {
        covered = false;
        break;
      }
    }
    if (!covered) {
      continue;
    }
    return {std::move(mapped), {}};
  }
  return {std::nullopt,
          QStringLiteral("Unsupported list source map: canonical markers, "
                         "explicit space continuations and closed fences "
                         "with matching callback offsets are required")};
}

std::vector<DefinitionCandidate> definitionInventory(const Document& document);
QByteArray definitionContext(const Document& document,
                             const std::vector<DefinitionCandidate>& definitions,
                             qsizetype first, qsizetype last, qsizetype& offset);

QuoteMapResult mapTopLevelQuote(const Document& document, size_t index) {
  if (index >= document.nodes.size() ||
      document.nodes[index].kind != NodeKind::kBlock ||
      document.nodes[index].type != MD_BLOCK_QUOTE ||
      document.nodes[index].parent != std::optional<size_t>(0)) {
    return {std::nullopt, QStringLiteral("Invalid quote target")};
  }
  const auto lines = listLines(document.source);
  for (size_t first = 0; first < lines.size(); ++first) {
    const auto range = scanQuote(document.source, lines, first);
    if (!range) {
      continue;
    }
    const auto quoteSource =
        document.source.mid(range->offset, range->size);
    bool matched = false;
    auto isolated = parse(quoteSource);
    if (isolated.document &&
        isolated.document->nodes[0].children.size() == 1) {
      const auto child = isolated.document->nodes[0].children.front();
      if (isolated.document->nodes[child].type == MD_BLOCK_QUOTE) {
        bool anchored = false;
        matched = sameListSources(document, index, *isolated.document, child,
                                  range->offset, anchored) &&
                  anchored;
      }
    }
    if (!matched) {
      const auto definitions = definitionInventory(document);
      qsizetype contextOffset = 0;
      const auto context = definitionContext(
          document, definitions, range->offset, range->offset + range->size,
          contextOffset);
      const auto header = context.left(contextOffset);
      QByteArray suffix;
      if (context.size() > contextOffset + range->size) {
        suffix = context.mid(contextOffset + range->size);
      }
      auto parsed = parse(header + quoteSource + suffix);
      if (parsed.document) {
        size_t child = parsed.document->nodes.size();
        for (auto candidate : parsed.document->nodes[0].children) {
          if (parsed.document->nodes[candidate].type == MD_BLOCK_QUOTE) {
            child = candidate;
            break;
          }
        }
        if (child < parsed.document->nodes.size()) {
          bool anchored = false;
          matched = sameListSources(document, index, *parsed.document, child,
                                    range->offset - contextOffset, anchored) &&
                    anchored;
        }
      }
    }
    if (matched) {
      return {QuoteSource{index, *range, {}, {}}, {}};
    }
  }
  return {std::nullopt,
          QStringLiteral("Unsupported quote source map: explicit prefixes on "
                         "all non-blank lines and matching callback offsets are "
                         "required")};
}

}  // namespace

namespace {
ListMapResult mapNestedList(const Document& document, size_t index);
QuoteMapResult mapNestedQuote(const Document& document, size_t index);
TableMapResult mapTopLevelTable(const Document& document, size_t index);
TableMapResult mapNestedTable(const Document& document, size_t index);
}  // namespace

ListMapResult mapList(const Document& document, size_t index) {
  if (index >= document.nodes.size() ||
      document.nodes[index].kind != NodeKind::kBlock ||
      (document.nodes[index].type != MD_BLOCK_UL &&
       document.nodes[index].type != MD_BLOCK_OL)) {
    return {std::nullopt, QStringLiteral("Invalid list target")};
  }
  if (document.nodes[index].parent == std::optional<size_t>(0)) {
    return mapTopLevelList(document, index);
  }
  return mapNestedList(document, index);
}

QuoteMapResult mapQuote(const Document& document, size_t index) {
  if (index >= document.nodes.size() ||
      document.nodes[index].kind != NodeKind::kBlock ||
      document.nodes[index].type != MD_BLOCK_QUOTE) {
    return {std::nullopt, QStringLiteral("Invalid quote target")};
  }
  if (document.nodes[index].parent == std::optional<size_t>(0)) {
    return mapTopLevelQuote(document, index);
  }
  return mapNestedQuote(document, index);
}

TableMapResult mapTable(const Document& document, size_t index) {
  if (index >= document.nodes.size() ||
      document.nodes[index].kind != NodeKind::kBlock ||
      document.nodes[index].type != MD_BLOCK_TABLE) {
    return {std::nullopt, QStringLiteral("Invalid table target")};
  }
  if (document.nodes[index].parent == std::optional<size_t>(0)) {
    return mapTopLevelTable(document, index);
  }
  return mapNestedTable(document, index);
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

int spanType(InlineStyle style) {
  switch (style) {
    case InlineStyle::kBold:
      return MD_SPAN_STRONG;
    case InlineStyle::kItalic:
      return MD_SPAN_EM;
    case InlineStyle::kStrikethrough:
      return MD_SPAN_DEL;
  }
  return -1;
}

size_t lineIndexAt(const std::vector<ListLine>& lines, qsizetype offset) {
  auto it = std::upper_bound(
      lines.begin(), lines.end(), offset,
      [](qsizetype value, const ListLine& line) { return value < line.start; });
  if (it == lines.begin()) {
    return 0;
  }
  return static_cast<size_t>(it - lines.begin()) - 1;
}

QByteArray listEol(const Document& document, const ListSource& list) {
  const auto bytes = document.source.mid(list.range.offset, list.range.size);
  const auto newline = bytes.indexOf('\n');
  if (newline >= 0) {
    return newline > 0 && bytes[newline - 1] == '\r' ? QByteArray("\r\n")
                                                      : QByteArray("\n");
  }
  if (bytes.contains('\r')) {
    return QByteArray("\r");
  }
  return documentEol(document.source);
}

bool listEndsAtEofWithoutEol(const Document& document, const ListSource& list) {
  const auto end = list.range.offset + list.range.size;
  return end == document.source.size() &&
         !document.source.endsWith('\n') && !document.source.endsWith('\r');
}

QByteArray itemContent(const Document& document, const ListItemSource& item) {
  QByteArray content;
  for (const auto& line : item.contentLines) {
    content += document.source.mid(line.offset, line.size);
  }
  return content;
}

std::optional<qsizetype> contentOffsetFor(const ListItemSource& item,
                                          qsizetype absolute) {
  qsizetype offset = 0;
  for (const auto& line : item.contentLines) {
    if (absolute >= line.offset && absolute < line.offset + line.size) {
      return offset + (absolute - line.offset);
    }
    offset += line.size;
  }
  return std::nullopt;
}

std::optional<qsizetype> firstRenderedContentOffset(
    const Document& document, const ListItemSource& item,
    const std::vector<DefinitionCandidate>& definitions) {
  QByteArray content = itemContent(document, item);
  std::vector<std::pair<qsizetype, qsizetype>> skipped;
  for (const auto& definition : definitions) {
    if (definition.begin < item.range.offset ||
        definition.range.offset + definition.range.size >
            item.range.offset + item.range.size) {
      continue;
    }
    const auto begin = contentOffsetFor(item, definition.begin);
    const auto last = definition.range.offset + definition.range.size - 1;
    const auto end = contentOffsetFor(item, last);
    if (begin && end) {
      skipped.emplace_back(*begin, *end + 1);
    }
  }
  qsizetype position = 0;
  while (position < content.size()) {
    const auto skippedEntry =
        std::find_if(skipped.begin(), skipped.end(),
                     [position](const auto& interval) {
                       return position >= interval.first &&
                              position < interval.second;
                     });
    if (skippedEntry != skipped.end()) {
      position = skippedEntry->second;
      continue;
    }
    if (content[position] == '\n' || content[position] == '\r') {
      ++position;
      continue;
    }
    if (content[position] == ' ' || content[position] == '\t') {
      auto scan = position;
      while (scan < content.size() && (content[scan] == ' ' ||
                                       content[scan] == '\t')) {
        ++scan;
      }
      if (scan >= content.size() || content[scan] == '\n' ||
          content[scan] == '\r') {
        position = scan;
        continue;
      }
    }
    return position;
  }
  return std::nullopt;
}

std::vector<QByteArray> itemGaps(const Document& document,
                                 const ListSource& list) {
  std::vector<QByteArray> gaps;
  for (size_t i = 0; i + 1 < list.items.size(); ++i) {
    const auto start = list.items[i].range.offset + list.items[i].range.size;
    const auto end = list.items[i + 1].range.offset;
    if (end < start) {
      return {};
    }
    gaps.push_back(document.source.mid(start, end - start));
  }
  return gaps;
}

QByteArray renderItem(const QByteArray& marker, const QByteArray& content,
                      const QByteArray& eol, bool trailingEol,
                      const QByteArray& parentPrefix = {},
                      const QByteArray& blankPrefix = {}) {
  auto normalized = normalizeEol(content, "\n");
  const bool hasTrailing = normalized.endsWith('\n');
  if (hasTrailing) {
    normalized.chop(1);
  }
  std::vector<QByteArray> lines;
  if (normalized.isEmpty()) {
    lines.emplace_back();
  } else {
    const auto parts = normalized.split('\n');
    for (const auto& part : parts) {
      lines.push_back(part);
    }
  }
  QByteArray output;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i == 0) {
      output += marker;
    } else {
      output += eol;
      if (lines[i].isEmpty()) {
        output += blankPrefix;
      } else {
        output += parentPrefix;
        output += QByteArray(marker.size(), ' ');
      }
    }
    output += lines[i];
  }
  if (trailingEol) {
    output += eol;
  }
  return output;
}

QByteArray gapFor(bool loose, const QByteArray& continuationPrefix,
                  const QByteArray& blankPrefix, const QByteArray& eol) {
  if (!loose) {
    return continuationPrefix;
  }
  return blankPrefix + eol + continuationPrefix;
}

QByteArray buildListSource(const std::vector<QByteArray>& contents,
                           const std::vector<QByteArray>& gaps, bool ordered,
                           int start, char delimiter, const QByteArray& eol,
                           bool trailingEol,
                           const QByteArray& continuationPrefix = {},
                           const QByteArray& blankPrefix = {}) {
  QByteArray output;
  for (size_t i = 0; i < contents.size(); ++i) {
    const QByteArray marker =
        ordered ? QByteArray::number(qint64(start) + qint64(i)) + delimiter + ' '
                : QByteArray(1, delimiter) + ' ';
    const bool last = i + 1 == contents.size();
    output += renderItem(marker, contents[i], eol, last ? trailingEol : true,
                         continuationPrefix, blankPrefix);
    if (!last && i < gaps.size()) {
      output += gaps[i];
    }
  }
  return output;
}

QByteArray stripContainerPrefix(const QByteArray& source,
                                const QByteArray& continuationPrefix,
                                const QByteArray& blankPrefix) {
  QByteArray result;
  const auto lines = listLines(source);
  for (size_t i = 0; i < lines.size(); ++i) {
    const auto& line = lines[i];
    const auto body = source.mid(line.start, line.bodyEnd - line.start);
    const auto ending = source.mid(line.bodyEnd, line.end - line.bodyEnd);
    if (i == 0) {
      result += body + ending;
      continue;
    }
    bool blank = false;
    if (body.startsWith(blankPrefix)) {
      const auto rest = body.mid(blankPrefix.size());
      blank = std::all_of(rest.begin(), rest.end(), [](char character) {
        return character == ' ' || character == '\t';
      });
    } else if (body.isEmpty()) {
      blank = true;
    }
    if (blank) {
      result += ending;
    } else if (body.startsWith(continuationPrefix)) {
      result += body.mid(continuationPrefix.size()) + ending;
    } else {
      result += body + ending;
    }
  }
  return result;
}

std::vector<QByteArray> splitNormalizedLines(const QByteArray& text,
                                             bool& trailing) {
  auto normalized = normalizeEol(text, "\n");
  trailing = normalized.endsWith('\n');
  if (trailing) {
    normalized.chop(1);
  }
  std::vector<QByteArray> lines;
  if (normalized.isEmpty() && !trailing) {
    lines.emplace_back();
    return lines;
  }
  for (auto& line : normalized.split('\n')) {
    lines.push_back(line);
  }
  return lines;
}

bool blankLine(const QByteArray& line) {
  for (auto character : line) {
    if (character != ' ' && character != '\t') {
      return false;
    }
  }
  return true;
}

bool taskPrefix(const QByteArray& content, qsizetype& position,
                qsizetype& markerSize) {
  position = 0;
  while (position < content.size() && position < 3 &&
         content[position] == ' ') {
    ++position;
  }
  if (position + 3 >= content.size()) {
    return false;
  }
  const auto& first = content[position];
  const auto& second = content[position + 1];
  const auto& third = content[position + 3];
  if (first != '[' || (second != ' ' && second != 'x' && second != 'X') ||
      content[position + 2] != ']' || third != ' ') {
    return false;
  }
  markerSize = 4;
  return true;
}

std::optional<ListMarker> listMarkerAt(const QByteArray& source,
                                       qsizetype offset) {
  const auto lines = listLines(source);
  if (lines.empty()) {
    return std::nullopt;
  }
  const auto index = lineIndexAt(lines, offset);
  if (offset < lines[index].start || offset > lines[index].bodyEnd) {
    return std::nullopt;
  }
  const ListLine view{offset, lines[index].bodyEnd, lines[index].end};
  return listMarker(source, view);
}

struct MappedItem {
  size_t list;
  ListSource source;
  size_t index;
};

std::optional<MappedItem> mapItem(const Document& document, size_t item,
                                  QString& error) {
  if (item >= document.nodes.size() ||
      document.nodes[item].kind != NodeKind::kBlock ||
      document.nodes[item].type != MD_BLOCK_LI) {
    error = QStringLiteral("Invalid list item target");
    return std::nullopt;
  }
  if (document.nodes.empty()) {
    error = QStringLiteral("Invalid list item target");
    return std::nullopt;
  }
  for (size_t child = 0; child < document.nodes.size(); ++child) {
    const auto& candidate = document.nodes[child];
    if (candidate.kind != NodeKind::kBlock ||
        (candidate.type != MD_BLOCK_UL && candidate.type != MD_BLOCK_OL)) {
      continue;
    }
    auto mapped = mapList(document, child);
    if (!mapped.list) {
      continue;
    }
    for (size_t i = 0; i < mapped.list->items.size(); ++i) {
      if (mapped.list->items[i].node == item) {
        return MappedItem{child, *mapped.list, i};
      }
    }
  }
  error = QStringLiteral(
      "Unsupported list item context: a mapped list is required");
  return std::nullopt;
}

bool hasNormalText(const Document& document, size_t index) {
  const auto& node = document.nodes[index];
  if (node.kind == NodeKind::kText && node.type == MD_TEXT_NORMAL &&
      !node.text.isEmpty()) {
    return true;
  }
  for (auto child : node.children) {
    if (hasNormalText(document, child)) {
      return true;
    }
  }
  return false;
}

bool taskEligible(const Document& document, size_t item) {
  const auto& node = document.nodes[item];
  if (node.children.empty()) {
    return false;
  }
  const auto first = node.children.front();
  const auto& value = document.nodes[first];
  if (value.kind == NodeKind::kBlock) {
    return value.type == MD_BLOCK_P && hasNormalText(document, first);
  }
  return value.kind == NodeKind::kText || value.kind == NodeKind::kSpan
             ? hasNormalText(document, first)
             : false;
}

struct ContextList {
  Document document;
  size_t root = 0;
};

QByteArray definitionContext(const Document& document,
                             const std::vector<DefinitionCandidate>& definitions,
                             qsizetype first, qsizetype last, qsizetype& offset) {
  QByteArray before;
  QByteArray after;
  for (const auto& definition : definitions) {
    const auto slice = document.source.mid(
        definition.begin,
        definition.range.offset + definition.range.size - definition.begin);
    const auto isolated = parse(slice);
    if (!isolated.document || !isolated.document->nodes[0].children.empty()) {
      continue;
    }
    if (definition.range.offset + definition.range.size <= first) {
      before += slice;
      if (!before.endsWith('\n') && !before.endsWith('\r')) {
        before += documentEol(document.source);
      }
    } else if (definition.begin >= last) {
      after += slice;
      if (!after.endsWith('\n') && !after.endsWith('\r')) {
        after += documentEol(document.source);
      }
    }
  }
  QByteArray context;
  if (!before.isEmpty()) {
    context += before;
    if (!context.endsWith("\n\n") && !context.endsWith("\r\n\r\n")) {
      context += documentEol(document.source);
    }
  }
  offset = context.size();
  context += document.source.mid(first, last - first);
  if (!after.isEmpty()) {
    while (!context.endsWith("\n\n") && !context.endsWith("\r\n\r\n")) {
      context += documentEol(document.source);
    }
    context += after;
  }
  return context;
}

std::optional<ContextList> parseListContext(
    const Document& document, const std::vector<DefinitionCandidate>& definitions,
    qsizetype first, qsizetype last, const QByteArray& listSource,
    QString& error) {
  qsizetype offset = 0;
  auto context = definitionContext(document, definitions, first, last, offset);
  // Replace the original list bytes with the candidate list source.
  const auto header = context.left(offset);
  QByteArray suffix;
  if (context.size() > offset + (last - first)) {
    suffix = context.mid(offset + (last - first));
  }
  const auto source = header + listSource + suffix;
  auto parsed = parse(source);
  if (!parsed.document) {
    error = QStringLiteral("Structural list parse failed");
    return std::nullopt;
  }
  size_t root = parsed.document->nodes.size();
  for (auto child : parsed.document->nodes[0].children) {
    const auto& node = parsed.document->nodes[child];
    if (node.kind == NodeKind::kBlock &&
        (node.type == MD_BLOCK_UL || node.type == MD_BLOCK_OL)) {
      root = child;
      break;
    }
  }
  if (root >= parsed.document->nodes.size()) {
    error = QStringLiteral("Structural list parse produced no list");
    return std::nullopt;
  }
  return ContextList{std::move(*parsed.document), root};
}

bool escapedAt(const QByteArray& source, qsizetype lineStart, qsizetype position) {
  qsizetype backslashes = 0;
  for (auto i = position; i > lineStart && source[i - 1] == '\\'; --i) {
    ++backslashes;
  }
  return backslashes % 2 == 1;
}

struct ScannedTableRow {
  bool valid = false;
  bool hasPipe = false;
  SourceRange row;
  std::vector<SourceRange> cells;
};

ScannedTableRow scanTableRow(const QByteArray& source, const ListLine& line) {
  ScannedTableRow result;
  result.row = {line.start, line.end - line.start};
  auto position = line.start;
  while (position < line.bodyEnd && source[position] == ' ') {
    ++position;
  }
  if (position - line.start > 3) {
    return result;
  }
  if (position < line.bodyEnd && source[position] == '|') {
    result.hasPipe = true;
    ++position;
  }
  auto end = line.bodyEnd;
  while (end > position && (source[end - 1] == ' ' || source[end - 1] == '\t')) {
    --end;
  }
  if (end > position && source[end - 1] == '|' &&
      !escapedAt(source, line.start, end - 1)) {
    result.hasPipe = true;
    --end;
  }
  auto cellStart = position;
  for (auto i = position; i < end; ++i) {
    if (source[i] == '\\' && i + 1 < end) {
      ++i;
      continue;
    }
    if (source[i] != '|') {
      continue;
    }
    result.hasPipe = true;
    auto cellEnd = i;
    while (cellStart < cellEnd &&
           (source[cellStart] == ' ' || source[cellStart] == '\t')) {
      ++cellStart;
    }
    while (cellEnd > cellStart &&
           (source[cellEnd - 1] == ' ' || source[cellEnd - 1] == '\t')) {
      --cellEnd;
    }
    result.cells.push_back({cellStart, cellEnd - cellStart});
    cellStart = i + 1;
  }
  auto cellEnd = end;
  while (cellStart < cellEnd &&
         (source[cellStart] == ' ' || source[cellStart] == '\t')) {
    ++cellStart;
  }
  while (cellEnd > cellStart &&
         (source[cellEnd - 1] == ' ' || source[cellEnd - 1] == '\t')) {
    --cellEnd;
  }
  result.cells.push_back({cellStart, cellEnd - cellStart});
  result.valid = true;
  return result;
}

bool delimiterCell(const QByteArray& source, SourceRange range) {
  auto position = range.offset;
  const auto end = range.offset + range.size;
  if (position < end && source[position] == ':') {
    ++position;
  }
  const auto hyphens = position;
  while (position < end && source[position] == '-') {
    ++position;
  }
  if (position == hyphens) {
    return false;
  }
  if (position < end && source[position] == ':') {
    ++position;
  }
  return position == end;
}

TableMapResult mapTopLevelTable(const Document& document, size_t index) {
  if (index >= document.nodes.size() ||
      document.nodes[index].kind != NodeKind::kBlock ||
      document.nodes[index].type != MD_BLOCK_TABLE ||
      document.nodes[index].parent != std::optional<size_t>(0)) {
    return {std::nullopt, QStringLiteral("Invalid table target")};
  }
  const auto& node = document.nodes[index];
  const auto columns = node.attributes["columns"].toInt();
  if (columns <= 0) {
    return {std::nullopt, QStringLiteral("Unsupported table dimensions")};
  }
  const auto lines = listLines(document.source);
  const auto definitions = definitionInventory(document);
  for (size_t first = 0; first + 1 < lines.size(); ++first) {
    const auto header = scanTableRow(document.source, lines[first]);
    if (!header.valid || !header.hasPipe ||
        header.cells.size() != size_t(columns)) {
      continue;
    }
    const auto delimiter = scanTableRow(document.source, lines[first + 1]);
    if (!delimiter.valid || !delimiter.hasPipe ||
        delimiter.cells.size() != size_t(columns)) {
      continue;
    }
    if (!std::all_of(delimiter.cells.begin(), delimiter.cells.end(),
                     [&document](SourceRange range) {
                       return delimiterCell(document.source, range);
                     })) {
      continue;
    }
    std::vector<std::vector<SourceRange>> bodyCells;
    std::vector<SourceRange> bodyRows;
    bool ragged = false;
    size_t last = first + 1;
    for (size_t i = first + 2; i < lines.size(); ++i) {
      if (blankListLine(document.source, lines[i])) {
        break;
      }
      const auto row = scanTableRow(document.source, lines[i]);
      if (!row.valid || !row.hasPipe) {
        break;
      }
      if (row.cells.size() != size_t(columns)) {
        ragged = true;
      }
      bodyCells.push_back(row.cells);
      bodyRows.push_back(row.row);
      last = i;
    }
    const auto end = lines[last].end;
    const SourceRange range{lines[first].start, end - lines[first].start};
    const auto tableSource = document.source.mid(range.offset, range.size);

    qsizetype contextOffset = 0;
    const auto context = definitionContext(document, definitions, range.offset,
                                           range.offset + range.size,
                                           contextOffset);
    const auto headerSource = context.left(contextOffset);
    QByteArray suffix;
    if (context.size() > contextOffset + range.size) {
      suffix = context.mid(contextOffset + range.size);
    }
    auto parsed = parse(headerSource + tableSource + suffix);
    if (!parsed.document) {
      continue;
    }
    size_t contextTable = parsed.document->nodes.size();
    for (auto child : parsed.document->nodes[0].children) {
      if (parsed.document->nodes[child].type == MD_BLOCK_TABLE) {
        contextTable = child;
        break;
      }
    }
    if (contextTable >= parsed.document->nodes.size()) {
      continue;
    }
    bool anchored = false;
    if (!sameListSources(document, index, *parsed.document, contextTable,
                         range.offset - contextOffset, anchored) ||
        !anchored) {
      continue;
    }
    if (ragged) {
      return {std::nullopt,
              QStringLiteral("Unsupported ragged table source")};
    }
    if (node.children.empty()) {
      continue;
    }
    size_t head = document.nodes.size();
    for (auto child : node.children) {
      if (document.nodes[child].type == MD_BLOCK_THEAD) {
        head = child;
        break;
      }
    }
    if (head >= document.nodes.size() || document.nodes[head].children.empty()) {
      continue;
    }
    const auto headerRow = document.nodes[head].children.front();
    if (document.nodes[headerRow].children.size() != size_t(columns)) {
      continue;
    }
    std::vector<TableRowSource> body;
    size_t bodyContainer = document.nodes.size();
    for (auto child : node.children) {
      if (document.nodes[child].type == MD_BLOCK_TBODY) {
        bodyContainer = child;
        break;
      }
    }
    if (bodyContainer < document.nodes.size()) {
      for (auto rowNode : document.nodes[bodyContainer].children) {
        if (document.nodes[rowNode].type != MD_BLOCK_TR) {
          continue;
        }
        body.push_back({rowNode, {}, {}});
      }
    }
    if (body.size() != bodyCells.size() ||
        bodyCells.size() != size_t(node.attributes["body_rows"].toInt())) {
      return {std::nullopt,
              QStringLiteral("Unsupported table row source map")};
    }

    TableSource mapped;
    mapped.node = index;
    mapped.range = range;
    mapped.columns = size_t(columns);
    mapped.delimiterRange = delimiter.row;
    mapped.delimiterCells = delimiter.cells;
    mapped.header.node = headerRow;
    mapped.header.range = header.row;
    const auto& headerCellNodes = document.nodes[headerRow].children;
    for (size_t i = 0; i < headerCellNodes.size(); ++i) {
      mapped.header.cells.push_back({headerCellNodes[i], header.cells[i]});
      mapped.alignments.push_back(static_cast<TableAlignment>(
          document.nodes[headerCellNodes[i]].attributes["align"].toInt()));
    }
    for (size_t row = 0; row < body.size(); ++row) {
      const auto& cellNodes = document.nodes[body[row].node].children;
      if (cellNodes.size() != size_t(columns)) {
        return {std::nullopt,
                QStringLiteral("Unsupported table row source map")};
      }
      body[row].range = bodyRows[row];
      for (size_t column = 0; column < cellNodes.size(); ++column) {
        body[row].cells.push_back({cellNodes[column], bodyCells[row][column]});
        if (static_cast<TableAlignment>(
                document.nodes[cellNodes[column]].attributes["align"].toInt()) !=
            mapped.alignments[column]) {
          return {std::nullopt,
                  QStringLiteral("Unsupported table alignment source map")};
        }
      }
    }
    mapped.body = std::move(body);
    return {std::move(mapped), {}};
  }
  return {std::nullopt,
          QStringLiteral("Unsupported table source map: canonical rows, "
                         "escaped pipes and matching callback offsets are "
                         "required")};
}

struct VirtualLine {
  qsizetype virtualStart;
  qsizetype realContentStart;
  qsizetype size;
};
struct VirtualContent {
  QByteArray source;
  std::vector<VirtualLine> lines;
  qsizetype realStart = 0;
  qsizetype realEnd = 0;
  QByteArray continuationPrefix;
  QByteArray blankLinePrefix;
};

struct VirtualParse {
  Document document;
  qsizetype contentOffset = 0;
  qsizetype contentEnd = 0;
};

qsizetype lineStartAt(const QByteArray& source, qsizetype offset) {
  return offset == 0 ? 0 : source.lastIndexOf('\n', offset - 1) + 1;
}

bool buildVirtualContent(const Document& document, size_t container,
                         VirtualContent& content, QString& error) {
  if (container >= document.nodes.size()) {
    error = QStringLiteral("Invalid nested container");
    return false;
  }
  const auto& node = document.nodes[container];
  if (node.kind != NodeKind::kBlock) {
    error = QStringLiteral("Invalid nested container");
    return false;
  }
  if (node.type == MD_BLOCK_LI) {
    if (!node.parent) {
      error = QStringLiteral("Invalid nested list item");
      return false;
    }
    const auto mapped = mapList(document, *node.parent);
    if (!mapped.list) {
      error = mapped.error;
      return false;
    }
    const ListItemSource* item = nullptr;
    for (const auto& candidate : mapped.list->items) {
      if (candidate.node == container) {
        item = &candidate;
        break;
      }
    }
    if (!item || item->contentLines.empty()) {
      error = QStringLiteral("Unsupported nested list item content");
      return false;
    }
    qsizetype virtualStart = 0;
    for (const auto& line : item->contentLines) {
      content.lines.push_back({virtualStart, line.offset, line.size});
      content.source += document.source.mid(line.offset, line.size);
      virtualStart += line.size;
    }
    content.realStart = item->contentLines.front().offset;
    const auto& last = item->contentLines.back();
    content.realEnd = last.offset + last.size;
    const auto firstLineStart =
        lineStartAt(document.source, item->contentLines.front().offset);
    const auto totalPrefix =
        item->contentLines.front().offset - firstLineStart;
    const auto listPrefix = mapped.list->continuationPrefix;
    const auto markerWidth =
        totalPrefix >= listPrefix.size() ? totalPrefix - listPrefix.size()
                                         : totalPrefix;
    content.continuationPrefix =
        listPrefix + QByteArray(markerWidth, ' ');
    content.blankLinePrefix = mapped.list->blankLinePrefix;
    return true;
  }
  if (node.type == MD_BLOCK_QUOTE) {
    const auto mapped = mapQuote(document, container);
    if (!mapped.quote) {
      error = mapped.error;
      return false;
    }
    const auto range = mapped.quote->range;
    qsizetype virtualStart = 0;
    bool first = true;
    for (const auto& line : listLines(document.source)) {
      if (line.end <= range.offset) {
        continue;
      }
      if (line.start >= range.offset + range.size) {
        break;
      }
      auto scanStart = line.start;
      if (first) {
        scanStart = range.offset;
      } else {
        const auto body =
            document.source.mid(line.start, line.bodyEnd - line.start);
        if (body.startsWith(mapped.quote->continuationPrefix)) {
          scanStart += mapped.quote->continuationPrefix.size();
        } else if (body.startsWith(mapped.quote->blankLinePrefix)) {
          scanStart += mapped.quote->blankLinePrefix.size();
        }
      }
      const ListLine view{scanStart, line.bodyEnd, line.end};
      const auto contentStart = quotePrefixEnd(document.source, view);
      if (!contentStart) {
        error = QStringLiteral("Unsupported quoted content line");
        return false;
      }
      content.lines.push_back(
          {virtualStart, *contentStart, line.end - *contentStart});
      content.source +=
          document.source.mid(*contentStart, line.end - *contentStart);
      virtualStart += line.end - *contentStart;
      if (first) {
        content.continuationPrefix =
            document.source.mid(line.start, *contentStart - line.start);
        content.blankLinePrefix = content.continuationPrefix;
        while (content.blankLinePrefix.endsWith(' ')) {
          content.blankLinePrefix.chop(1);
        }
        first = false;
      }
    }
    if (content.lines.empty()) {
      error = QStringLiteral("Unsupported empty quote content");
      return false;
    }
    content.realStart = content.lines.front().realContentStart;
    const auto& last = content.lines.back();
    content.realEnd = last.realContentStart + last.size;
    return true;
  }
  error = QStringLiteral("Unsupported nested container type");
  return false;
}

std::optional<VirtualParse> parseVirtual(const Document& document,
                                         const VirtualContent& content,
                                         QString& error) {
  const auto definitions = definitionInventory(document);
  qsizetype offset = 0;
  const auto context = definitionContext(document, definitions, content.realStart,
                                         content.realEnd, offset);
  const auto header = context.left(offset);
  QByteArray suffix;
  if (context.size() > offset + (content.realEnd - content.realStart)) {
    suffix = context.mid(offset + (content.realEnd - content.realStart));
  }
  auto parsed = parse(header + content.source + suffix);
  if (!parsed.document) {
    error = QStringLiteral("Nested context parse failed");
    return std::nullopt;
  }
  return VirtualParse{std::move(*parsed.document), header.size(),
                      header.size() + content.source.size()};
}

qsizetype virtualToReal(const VirtualContent& content, qsizetype offset) {
  if (content.lines.empty()) {
    return content.realStart;
  }
  const auto value = qBound<qsizetype>(0, offset, content.source.size());
  for (const auto& line : content.lines) {
    if (value < line.virtualStart + line.size) {
      return line.realContentStart + (value - line.virtualStart);
    }
  }
  const auto& last = content.lines.back();
  return last.realContentStart + last.size;
}

qsizetype virtualToRealEnd(const VirtualContent& content, qsizetype offset) {
  if (content.lines.empty()) {
    return content.realStart;
  }
  const auto value = qBound<qsizetype>(0, offset, content.source.size());
  if (value == 0) {
    return content.realStart;
  }
  for (size_t i = content.lines.size(); i-- > 0;) {
    const auto& line = content.lines[i];
    if (value > line.virtualStart) {
      return line.realContentStart +
             qMin(value - line.virtualStart, line.size);
    }
  }
  return content.realStart;
}

std::optional<size_t> childPosition(const Document& document, size_t parent,
                                    size_t child) {
  if (parent >= document.nodes.size()) {
    return std::nullopt;
  }
  size_t position = 0;
  bool inInlineRun = false;
  for (auto candidate : document.nodes[parent].children) {
    if (candidate == child) {
      return position;
    }
    const auto& node = document.nodes[candidate];
    if (node.kind == NodeKind::kBlock) {
      ++position;
      inInlineRun = false;
    } else if (!inInlineRun) {
      // Tight list items keep paragraph inline children directly under LI.
      // A contiguous inline run corresponds to one implicit paragraph block.
      ++position;
      inInlineRun = true;
    }
  }
  return std::nullopt;
}

ListSource convertListSource(const Document& document, size_t realNode,
                             const VirtualContent& content,
                             const VirtualParse& parsed,
                             const ListSource& virtualSource) {
  ListSource result;
  result.node = realNode;
  const auto start =
      virtualToReal(content, virtualSource.range.offset - parsed.contentOffset);
  const auto end = virtualToRealEnd(
      content, virtualSource.range.offset + virtualSource.range.size -
                   parsed.contentOffset);
  result.range = {start, end - start};
  result.continuationPrefix = content.continuationPrefix;
  result.blankLinePrefix = content.blankLinePrefix;
  const auto& realChildren = document.nodes[realNode].children;
  for (const auto& virtualItem : virtualSource.items) {
    if (result.items.size() >= realChildren.size()) {
      break;
    }
    ListItemSource item;
    item.node = realChildren[result.items.size()];
    const auto itemStart =
        virtualToReal(content, virtualItem.range.offset - parsed.contentOffset);
    const auto itemEnd = virtualToRealEnd(
        content, virtualItem.range.offset + virtualItem.range.size -
                     parsed.contentOffset);
    item.range = {itemStart, itemEnd - itemStart};
    for (const auto& line : virtualItem.contentLines) {
      const auto lineStart =
          virtualToReal(content, line.offset - parsed.contentOffset);
      const auto lineEnd = virtualToRealEnd(
          content, line.offset + line.size - parsed.contentOffset);
      item.contentLines.push_back({lineStart, lineEnd - lineStart});
    }
    result.items.push_back(std::move(item));
  }
  return result;
}

ListMapResult mapNestedList(const Document& document, size_t index) {
  if (index >= document.nodes.size() || !document.nodes[index].parent) {
    return {std::nullopt, QStringLiteral("Invalid list target")};
  }
  const auto parent = *document.nodes[index].parent;
  if (parent >= document.nodes.size()) {
    return {std::nullopt, QStringLiteral("Invalid list target")};
  }
  const auto type = document.nodes[parent].type;
  if (document.nodes[parent].kind != NodeKind::kBlock ||
      (type != MD_BLOCK_LI && type != MD_BLOCK_QUOTE)) {
    return {std::nullopt,
            QStringLiteral("Unsupported nested list parent: LI or QUOTE is "
                           "required")};
  }
  QString error;
  VirtualContent content;
  if (!buildVirtualContent(document, parent, content, error)) {
    return {std::nullopt, error};
  }
  auto parsed = parseVirtual(document, content, error);
  if (!parsed) {
    return {std::nullopt, error};
  }
  const auto position = childPosition(document, parent, index);
  if (!position ||
      *position >= parsed->document.nodes[0].children.size()) {
    return {std::nullopt, QStringLiteral("Nested list context mismatch")};
  }
  const auto virtualNode =
      parsed->document.nodes[0].children[*position];
  const auto& virtualValue = parsed->document.nodes[virtualNode];
  if (virtualValue.kind != NodeKind::kBlock ||
      (virtualValue.type != MD_BLOCK_UL && virtualValue.type != MD_BLOCK_OL)) {
    return {std::nullopt, QStringLiteral("Nested list context mismatch")};
  }
  const auto virtualList = mapTopLevelList(parsed->document, virtualNode);
  if (!virtualList.list) {
    return {std::nullopt, virtualList.error};
  }
  return {convertListSource(document, index, content, *parsed,
                            *virtualList.list),
          {}};
}

QuoteMapResult mapNestedQuote(const Document& document, size_t index) {
  if (index >= document.nodes.size() || !document.nodes[index].parent) {
    return {std::nullopt, QStringLiteral("Invalid quote target")};
  }
  const auto parent = *document.nodes[index].parent;
  if (parent >= document.nodes.size()) {
    return {std::nullopt, QStringLiteral("Invalid quote target")};
  }
  const auto type = document.nodes[parent].type;
  if (document.nodes[parent].kind != NodeKind::kBlock ||
      (type != MD_BLOCK_LI && type != MD_BLOCK_QUOTE)) {
    return {std::nullopt,
            QStringLiteral("Unsupported nested quote parent: LI or QUOTE is "
                           "required")};
  }
  QString error;
  VirtualContent content;
  if (!buildVirtualContent(document, parent, content, error)) {
    return {std::nullopt, error};
  }
  auto parsed = parseVirtual(document, content, error);
  if (!parsed) {
    return {std::nullopt, error};
  }
  const auto position = childPosition(document, parent, index);
  if (!position ||
      *position >= parsed->document.nodes[0].children.size()) {
    return {std::nullopt, QStringLiteral("Nested quote context mismatch")};
  }
  const auto virtualNode =
      parsed->document.nodes[0].children[*position];
  const auto& virtualValue = parsed->document.nodes[virtualNode];
  if (virtualValue.kind != NodeKind::kBlock ||
      virtualValue.type != MD_BLOCK_QUOTE) {
    return {std::nullopt, QStringLiteral("Nested quote context mismatch")};
  }
  const auto virtualQuote = mapTopLevelQuote(parsed->document, virtualNode);
  if (!virtualQuote.quote) {
    return {std::nullopt, virtualQuote.error};
  }
  const auto start = virtualToReal(
      content, virtualQuote.quote->range.offset - parsed->contentOffset);
  const auto end = virtualToRealEnd(
      content, virtualQuote.quote->range.offset +
                   virtualQuote.quote->range.size - parsed->contentOffset);
  return {QuoteSource{index, {start, end - start},
                      content.continuationPrefix, content.blankLinePrefix},
          {}};
}

TableSource convertTableSource(const Document& document, size_t realNode,
                               const VirtualContent& content,
                               const VirtualParse& parsed,
                               const TableSource& virtualSource) {
  TableSource result;
  result.node = realNode;
  result.columns = virtualSource.columns;
  result.continuationPrefix = content.continuationPrefix;
  result.blankLinePrefix = content.blankLinePrefix;
  const auto convert = [&content, &parsed](SourceRange range) {
    const auto start =
        virtualToReal(content, range.offset - parsed.contentOffset);
    const auto end = virtualToRealEnd(
        content, range.offset + range.size - parsed.contentOffset);
    return SourceRange{start, end - start};
  };
  const auto convertCell = [&convert](const TableCellSource& cell) {
    return TableCellSource{cell.node, convert(cell.range)};
  };
  result.range = convert(virtualSource.range);
  result.header.range = convert(virtualSource.header.range);
  result.delimiterRange = convert(virtualSource.delimiterRange);
  for (const auto& cell : virtualSource.header.cells) {
    result.header.cells.push_back(convertCell(cell));
  }
  for (const auto& cell : virtualSource.delimiterCells) {
    result.delimiterCells.push_back(convert(cell));
  }
  for (const auto& row : virtualSource.body) {
    TableRowSource converted;
    converted.range = convert(row.range);
    for (const auto& cell : row.cells) {
      converted.cells.push_back(convertCell(cell));
    }
    result.body.push_back(std::move(converted));
  }

  size_t head = document.nodes.size();
  for (auto child : document.nodes[realNode].children) {
    if (document.nodes[child].type == MD_BLOCK_THEAD) {
      head = child;
      break;
    }
  }
  if (head < document.nodes.size() && !document.nodes[head].children.empty()) {
    const auto headerRow = document.nodes[head].children.front();
    result.header.node = headerRow;
    const auto& headerCells = document.nodes[headerRow].children;
    for (size_t i = 0; i < headerCells.size() && i < result.header.cells.size();
         ++i) {
      result.header.cells[i].node = headerCells[i];
      result.alignments.push_back(static_cast<TableAlignment>(
          document.nodes[headerCells[i]].attributes["align"].toInt()));
    }
  }
  size_t bodyContainer = document.nodes.size();
  for (auto child : document.nodes[realNode].children) {
    if (document.nodes[child].type == MD_BLOCK_TBODY) {
      bodyContainer = child;
      break;
    }
  }
  if (bodyContainer < document.nodes.size()) {
    const auto& rows = document.nodes[bodyContainer].children;
    for (size_t row = 0; row < rows.size() && row < result.body.size(); ++row) {
      result.body[row].node = rows[row];
      const auto& cells = document.nodes[rows[row]].children;
      for (size_t column = 0;
           column < cells.size() && column < result.body[row].cells.size();
           ++column) {
        result.body[row].cells[column].node = cells[column];
      }
    }
  }
  return result;
}

TableMapResult mapNestedTable(const Document& document, size_t index) {
  if (index >= document.nodes.size() || !document.nodes[index].parent) {
    return {std::nullopt, QStringLiteral("Invalid table target")};
  }
  const auto parent = *document.nodes[index].parent;
  if (parent >= document.nodes.size()) {
    return {std::nullopt, QStringLiteral("Invalid table target")};
  }
  const auto type = document.nodes[parent].type;
  if (document.nodes[parent].kind != NodeKind::kBlock ||
      (type != MD_BLOCK_LI && type != MD_BLOCK_QUOTE)) {
    return {std::nullopt,
            QStringLiteral("Unsupported nested table parent: LI or QUOTE is "
                           "required")};
  }
  QString error;
  VirtualContent content;
  if (!buildVirtualContent(document, parent, content, error)) {
    return {std::nullopt, error};
  }
  auto parsed = parseVirtual(document, content, error);
  if (!parsed) {
    return {std::nullopt, error};
  }
  const auto position = childPosition(document, parent, index);
  if (!position ||
      *position >= parsed->document.nodes[0].children.size()) {
    return {std::nullopt, QStringLiteral("Nested table context mismatch")};
  }
  const auto virtualNode =
      parsed->document.nodes[0].children[*position];
  if (parsed->document.nodes[virtualNode].type != MD_BLOCK_TABLE) {
    return {std::nullopt, QStringLiteral("Nested table context mismatch")};
  }
  const auto virtualTable =
      mapTopLevelTable(parsed->document, virtualNode);
  if (!virtualTable.table) {
    return {std::nullopt, virtualTable.error};
  }
  return {convertTableSource(document, index, content, *parsed,
                             *virtualTable.table),
          {}};
}

size_t copySubtree(Document& destination, const Document& source, size_t index,
                   std::optional<size_t> parent) {
  const auto newIndex = destination.nodes.size();
  destination.nodes.push_back(source.nodes[index]);
  destination.nodes[newIndex].parent = parent;
  destination.nodes[newIndex].children.clear();
  for (auto child : source.nodes[index].children) {
    const auto copied = copySubtree(destination, source, child, newIndex);
    destination.nodes[newIndex].children.push_back(copied);
  }
  return newIndex;
}

Document graftList(const Document& original, size_t oldList,
                   const ContextList& parsed) {
  auto expected = original;
  if (oldList >= original.nodes.size()) {
    return expected;
  }
  const auto parent = original.nodes[oldList].parent.value_or(0);
  auto& siblings = expected.nodes[parent].children;
  const auto position = std::find(siblings.begin(), siblings.end(), oldList);
  if (position == siblings.end()) {
    return expected;
  }
  const auto newIndex =
      copySubtree(expected, parsed.document, parsed.root, parent);
  *position = newIndex;
  return expected;
}

Document removeListNode(const Document& original, size_t list) {
  auto expected = original;
  if (list >= original.nodes.size()) {
    return expected;
  }
  const auto parent = original.nodes[list].parent.value_or(0);
  auto& siblings = expected.nodes[parent].children;
  const auto position = std::find(siblings.begin(), siblings.end(), list);
  if (position != siblings.end()) {
    siblings.erase(position);
  }
  return expected;
}

void updateLiTightness(Document& document, size_t li) {
  if (li >= document.nodes.size() || !document.nodes[li].parent) {
    return;
  }
  const auto list = *document.nodes[li].parent;
  if (list >= document.nodes.size() ||
      (document.nodes[list].type != MD_BLOCK_UL &&
       document.nodes[list].type != MD_BLOCK_OL)) {
    return;
  }
  auto& node = document.nodes[list];
  bool tight = node.attributes["tight"].toBool();
  const auto count = document.nodes[li].children.size();
  if (count >= 2) {
    tight = false;
  } else if (count == 1 && node.children.size() == 1) {
    const auto& child = document.nodes[document.nodes[li].children.front()];
    const bool paragraph =
        child.kind != NodeKind::kBlock || child.type == MD_BLOCK_P;
    if (paragraph) {
      tight = true;
    }
  }
  node.attributes["tight"] = tight;
  if (!tight || li >= document.nodes.size()) {
    return;
  }
  auto& children = document.nodes[li].children;
  if (children.size() != 1) {
    return;
  }
  const auto only = children.front();
  if (document.nodes[only].kind == NodeKind::kBlock &&
      document.nodes[only].type == MD_BLOCK_P) {
    auto inner = document.nodes[only].children;
    for (auto child : inner) {
      document.nodes[child].parent = li;
    }
    children = std::move(inner);
  }
}

std::vector<QByteArray> referenceLabels(
    const QByteArray& source,
    const std::vector<DefinitionCandidate>& definitions,
    SourceRange excluded) {
  std::vector<QByteArray> labels;
  const auto lines = listLines(source);
  ListFence fence;
  for (const auto& line : lines) {
    if (fence.marker) {
      updateListFence(source, line.start, line.bodyEnd, fence);
      continue;
    }
    updateListFence(source, line.start, line.bodyEnd, fence);
    if (fence.marker) {
      continue;
    }
    for (auto position = line.start; position < line.bodyEnd; ++position) {
      if (position >= excluded.offset &&
          position < excluded.offset + excluded.size) {
        continue;
      }
      const bool inDefinition =
          std::any_of(definitions.begin(), definitions.end(),
                      [position](const DefinitionCandidate& definition) {
                        return position >= definition.range.offset &&
                               position <
                                   definition.range.offset + definition.range.size;
                      });
      if (inDefinition) {
        continue;
      }
      if (source[position] == '\\') {
        ++position;
        continue;
      }
      if (source[position] == '`') {
        qsizetype run = 0;
        while (position + run < line.bodyEnd &&
               source[position + run] == '`') {
          ++run;
        }
        const QByteArray closing(run, '`');
        const auto end = source.indexOf(closing, position + run);
        if (end >= 0 && end < line.bodyEnd) {
          position = end + run - 1;
          continue;
        }
      }
      if (source[position] != '[') {
        continue;
      }
      qsizetype depth = 0;
      qsizetype close = -1;
      for (auto scan = position; scan < line.bodyEnd; ++scan) {
        if (source[scan] == '\\') {
          ++scan;
        } else if (source[scan] == '[') {
          ++depth;
        } else if (source[scan] == ']') {
          if (--depth == 0) {
            close = scan;
            break;
          }
        }
      }
      if (close < 0) {
        continue;
      }
      const auto text = source.mid(position + 1, close - position - 1);
      const auto after = close + 1;
      if (after < line.bodyEnd && source[after] == ':') {
        position = close;
        continue;
      }
      if (after < line.bodyEnd && source[after] == '(') {
        position = close;
        continue;
      }
      if (after < line.bodyEnd && source[after] == '[') {
        qsizetype end = -1;
        for (auto scan = after + 1; scan < line.bodyEnd; ++scan) {
          if (source[scan] == '\\') {
            ++scan;
          } else if (source[scan] == ']') {
            end = scan;
            break;
          }
        }
        if (end < 0) {
          position = close;
          continue;
        }
        const auto explicitLabel = source.mid(after + 1, end - after - 1);
        labels.push_back(explicitLabel.isEmpty() ? text : explicitLabel);
        position = end;
        continue;
      }
      labels.push_back(text);
      position = close;
    }
  }
  return labels;
}

bool definitionUsed(const Document& document,
                    const std::vector<DefinitionCandidate>& definitions,
                    size_t definition, SourceRange excluded,
                    QString& error) {
  for (const auto& label : referenceLabels(document.source, definitions,
                                            excluded)) {
    const auto equivalent = labelsEquivalent(definitions[definition].text, label);
    if (!equivalent.has_value()) {
      error = QStringLiteral("Unsupported reference label equivalence");
      return true;
    }
    if (*equivalent) {
      return true;
    }
  }
  return false;
}

bool definitionWinning(const std::vector<DefinitionCandidate>& definitions,
                       size_t index, bool& indeterminate) {
  for (size_t i = 0; i < index; ++i) {
    const auto equivalent =
        labelsEquivalent(definitions[i].text, definitions[index].text);
    if (!equivalent.has_value()) {
      indeterminate = true;
      return false;
    }
    if (*equivalent) {
      return false;
    }
  }
  return true;
}

EditResult insertListItemSource(const Document& document,
                                const InsertListItem& edit) {
  auto mapped = mapList(document, edit.list);
  if (!mapped.list) {
    return {std::nullopt, mapped.error};
  }
  const auto& list = *mapped.list;
  if (edit.position > list.items.size()) {
    return {std::nullopt, QStringLiteral("Invalid list insertion position")};
  }
  const auto marker = listMarkerAt(document.source, list.range.offset);
  if (!marker) {
    return {std::nullopt, QStringLiteral("Unsupported list marker")};
  }
  const auto& node = document.nodes[edit.list];
  const bool ordered = node.type == MD_BLOCK_OL;
  const int start = ordered ? node.attributes["start"].toInt() : 0;
  if (ordered && (start < 0 || start > 999999999)) {
    return {std::nullopt, QStringLiteral("Ordered list number overflow")};
  }
  if (ordered && qint64(start) + qint64(list.items.size()) > 999999999) {
    return {std::nullopt, QStringLiteral("Ordered list number overflow")};
  }
  if (edit.blocks.isEmpty() || edit.blocks.contains('\0') ||
      !validUtf8(edit.blocks)) {
    return {std::nullopt, QStringLiteral("Invalid list item fragment")};
  }
  const auto eol = listEol(document, list);
  bool trailing = false;
  auto lines = splitNormalizedLines(edit.blocks, trailing);
  while (!lines.empty() && blankLine(lines.front())) {
    lines.erase(lines.begin());
  }
  while (!lines.empty() && blankLine(lines.back())) {
    lines.pop_back();
  }
  if (lines.empty()) {
    return {std::nullopt, QStringLiteral("Invalid list item fragment")};
  }
  QByteArray joined;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) {
      joined += '\n';
    }
    joined += lines[i];
  }
  joined += '\n';
  auto parsedFragment = parse(joined);
  if (!parsedFragment.document ||
      parsedFragment.document->nodes[0].children.empty()) {
    return {std::nullopt, QStringLiteral("Invalid list item fragment")};
  }
  qsizetype prefixPosition = 0;
  qsizetype prefixSize = 0;
  if (taskPrefix(lines.front(), prefixPosition, prefixSize)) {
    lines.front().insert(prefixPosition, '\\');
  }
  QByteArray content;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) {
      content += eol;
    }
    content += lines[i];
  }
  content += eol;

  const auto listLoose = !node.attributes["tight"].toBool();
  auto contents = std::vector<QByteArray>{};
  for (const auto& item : list.items) {
    contents.push_back(itemContent(document, item));
  }
  auto gaps = itemGaps(document, list);
  contents.insert(contents.begin() + qsizetype(edit.position), content);
  const auto newGap = gapFor(listLoose, list.continuationPrefix,
                             list.blankLinePrefix, eol);
  if (gaps.empty()) {
    gaps.assign(contents.size() - 1, newGap);
  } else if (edit.position == 0) {
    gaps.insert(gaps.begin(), newGap);
  } else if (edit.position == contents.size() - 1) {
    gaps.push_back(newGap);
  } else {
    gaps.insert(gaps.begin() + qsizetype(edit.position),
                list.continuationPrefix);
  }
  const auto trailingEol = !listEndsAtEofWithoutEol(document, list);
  const auto newSource = buildListSource(
      contents, gaps, ordered, start, marker->delimiter, eol, trailingEol,
      list.continuationPrefix, list.blankLinePrefix);
  const auto canonicalSource = stripContainerPrefix(
      newSource, list.continuationPrefix, list.blankLinePrefix);
  qsizetype insertedOffset = 0;
  for (size_t i = 0; i < edit.position; ++i) {
    const QByteArray itemMarker =
        ordered ? QByteArray::number(qint64(start) + qint64(i)) +
                      marker->delimiter + ' '
                : QByteArray(1, marker->delimiter) + ' ';
    insertedOffset +=
        renderItem(itemMarker, contents[i], eol, true,
                   list.continuationPrefix, list.blankLinePrefix)
            .size();
    if (i < gaps.size()) {
      insertedOffset += gaps[i].size();
    }
  }
  const QByteArray itemMarker =
      ordered ? QByteArray::number(qint64(start) + qint64(edit.position)) +
                    marker->delimiter + ' '
              : QByteArray(1, marker->delimiter) + ' ';
  const auto insertedSize =
      renderItem(itemMarker, content, eol,
                 edit.position == contents.size() - 1 ? trailingEol : true,
                 list.continuationPrefix, list.blankLinePrefix)
          .size();
  const auto insertedStart = list.range.offset + insertedOffset;
  auto tentativeSource = document.source;
  tentativeSource.replace(list.range.offset, list.range.size, newSource);
  auto tentative = parse(tentativeSource);
  if (!tentative.document) {
    return {std::nullopt, QStringLiteral("Structural list parse failed")};
  }
  const auto tentativeDefinitions = definitionInventory(*tentative.document);
  bool introducedDefinitions = false;
  for (size_t i = 0; i < tentativeDefinitions.size(); ++i) {
    const auto& definition = tentativeDefinitions[i];
    if (definition.begin < insertedStart ||
        definition.range.offset + definition.range.size >
            insertedStart + insertedSize) {
      continue;
    }
    introducedDefinitions = true;
    for (size_t j = 0; j < tentativeDefinitions.size(); ++j) {
      if (i == j) {
        continue;
      }
      const auto equivalent =
          labelsEquivalent(definition.text, tentativeDefinitions[j].text);
      if (!equivalent.has_value()) {
        return {std::nullopt,
                QStringLiteral("Unsupported definition label equivalence")};
      }
      if (*equivalent) {
        return {std::nullopt, QStringLiteral("Definition conflict")};
      }
    }
  }
  const auto definitions = definitionInventory(document);
  QString contextError;
  auto parsed = parseListContext(document, definitions, list.range.offset,
                                 list.range.offset + list.range.size,
                                 canonicalSource, contextError);
  if (!parsed) {
    return {std::nullopt, contextError};
  }
  const auto& parsedList = parsed->document.nodes[parsed->root];
  if (parsedList.children.size() != list.items.size() + 1) {
    return {std::nullopt, QStringLiteral("Structural list mismatch")};
  }
  if (ordered && parsedList.attributes["start"].toInt() != start) {
    return {std::nullopt, QStringLiteral("Structural ordered list mismatch")};
  }
  const auto& created =
      parsed->document.nodes[parsedList.children[edit.position]];
  if (created.attributes["task"].toBool()) {
    return {std::nullopt, QStringLiteral("Inserted list item became a task")};
  }
  const auto fragmentBlocks =
      parsedFragment.document->nodes[0].children.size();
  const auto fragmentDefinitions =
      definitionInventory(*parsedFragment.document).size();
  const auto expectedTight =
      node.attributes["tight"].toBool() &&
      fragmentBlocks + fragmentDefinitions <= 1;
  if (parsedList.attributes["tight"].toBool() != expectedTight) {
    return {std::nullopt, QStringLiteral("Structural tightness mismatch")};
  }
  for (size_t i = 0; i < list.items.size(); ++i) {
    const auto parsedIndex = i < edit.position ? i : i + 1;
    const auto& oldItem = document.nodes[list.items[i].node];
    const auto& newItem =
        parsed->document.nodes[parsedList.children[parsedIndex]];
    if (oldItem.attributes["task"] != newItem.attributes["task"] ||
        oldItem.attributes["checked"] != newItem.attributes["checked"]) {
      return {std::nullopt, QStringLiteral("Structural task-state mismatch")};
    }
  }
  auto expected = graftList(document, edit.list, *parsed);
  auto result = apply(document, expected, list.range, newSource);
  if (!result.source && introducedDefinitions) {
    return {std::nullopt,
            QStringLiteral("Binding conflict: new definition changes existing "
                           "reference semantics")};
  }
  return result;
}

EditResult removeListItemSource(const Document& document,
                                const RemoveListItem& edit) {
  QString error;
  auto mapped = mapItem(document, edit.item, error);
  if (!mapped) {
    return {std::nullopt, error};
  }
  const auto& list = mapped->source;
  const auto& item = list.items[mapped->index];
  const auto definitions = definitionInventory(document);
  bool removedDefinition = false;
  for (size_t i = 0; i < definitions.size(); ++i) {
    const auto& definition = definitions[i];
    if (definition.begin < item.range.offset ||
        definition.range.offset + definition.range.size >
            item.range.offset + item.range.size) {
      continue;
    }
    removedDefinition = true;
    bool indeterminate = false;
    if (!definitionWinning(definitions, i, indeterminate)) {
      if (indeterminate) {
        return {std::nullopt,
                QStringLiteral("Unsupported definition label equivalence")};
      }
      continue;
    }
    if (definitionUsed(document, definitions, i, item.range, error)) {
      if (!error.isEmpty()) {
        return {std::nullopt, error};
      }
      return {std::nullopt, QStringLiteral("Definition in use")};
    }
  }
  if (list.items.size() == 1) {
    const auto parent = document.nodes[mapped->list].parent;
    if (parent && document.nodes[*parent].type == MD_BLOCK_QUOTE &&
        document.nodes[*parent].children.size() == 1) {
      return {std::nullopt,
              QStringLiteral("Unsupported cascade: nested list is the only "
                             "block of a quote")};
    }
    auto expected = removeListNode(document, mapped->list);
    if (parent && expected.nodes[*parent].type == MD_BLOCK_LI) {
      updateLiTightness(expected, *parent);
    }
    auto result = apply(document, expected, list.range, QByteArray{});
    if (!result.source && removedDefinition) {
      return {std::nullopt, QStringLiteral("Definition in use")};
    }
    return result;
  }
  auto contents = std::vector<QByteArray>{};
  for (const auto& value : list.items) {
    contents.push_back(itemContent(document, value));
  }
  auto gaps = itemGaps(document, list);
  contents.erase(contents.begin() + qsizetype(mapped->index));
  if (!gaps.empty()) {
    if (mapped->index == 0) {
      gaps.erase(gaps.begin());
    } else if (mapped->index + 1 == list.items.size()) {
      gaps.erase(gaps.begin() + qsizetype(gaps.size()) - 1);
    } else {
      gaps.erase(gaps.begin() + qsizetype(mapped->index));
    }
  }
  const auto marker = listMarkerAt(document.source, list.range.offset);
  if (!marker) {
    return {std::nullopt, QStringLiteral("Unsupported list marker")};
  }
  const auto& listNode = document.nodes[mapped->list];
  const auto ordered = listNode.type == MD_BLOCK_OL;
  const auto start = ordered ? listNode.attributes["start"].toInt() : 0;
  const auto eol = listEol(document, list);
  const auto trailingEol = !listEndsAtEofWithoutEol(document, list);
  const auto newSource = buildListSource(
      contents, gaps, ordered, start, marker->delimiter, eol, trailingEol,
      list.continuationPrefix, list.blankLinePrefix);
  const auto canonicalSource = stripContainerPrefix(
      newSource, list.continuationPrefix, list.blankLinePrefix);
  QString contextError;
  auto parsed = parseListContext(document, definitions, list.range.offset,
                                 list.range.offset + list.range.size,
                                 canonicalSource, contextError);
  if (!parsed) {
    return {std::nullopt, contextError};
  }
  const auto& parsedList = parsed->document.nodes[parsed->root];
  if (parsedList.children.size() != list.items.size() - 1) {
    return {std::nullopt, QStringLiteral("Structural list mismatch")};
  }
  if (ordered && parsedList.attributes["start"].toInt() != start) {
    return {std::nullopt, QStringLiteral("Structural ordered list mismatch")};
  }
  bool singleParagraph = false;
  if (list.items.size() == 2) {
    const auto remaining = mapped->index == 0 ? size_t(1) : size_t(0);
    const auto& remainingNode = document.nodes[list.items[remaining].node];
    const auto& remainingSource = list.items[remaining];
    const bool hasDefinition =
        std::any_of(definitions.begin(), definitions.end(),
                    [&remainingSource](const DefinitionCandidate& definition) {
                      return definition.begin >=
                                 remainingSource.range.offset &&
                             definition.range.offset + definition.range.size <=
                                 remainingSource.range.offset +
                                     remainingSource.range.size;
                    });
    if (remainingNode.children.size() == 1 && !hasDefinition) {
      const auto& child = document.nodes[remainingNode.children[0]];
      singleParagraph = child.kind != NodeKind::kBlock ||
                        child.type == MD_BLOCK_P;
    }
  }
  const auto expectedTight =
      list.items.size() == 2 && singleParagraph
          ? true
          : document.nodes[mapped->list].attributes["tight"].toBool();
  if (parsedList.attributes["tight"].toBool() != expectedTight) {
    return {std::nullopt, QStringLiteral("Structural tightness mismatch")};
  }
  for (size_t i = 0; i < list.items.size(); ++i) {
    if (i == mapped->index) {
      continue;
    }
    const auto target = i < mapped->index ? i : i - 1;
    if (target >= parsedList.children.size()) {
      return {std::nullopt, QStringLiteral("Structural list mismatch")};
    }
    const auto& oldItem = document.nodes[list.items[i].node];
    const auto& newItem =
        parsed->document.nodes[parsedList.children[target]];
    if (oldItem.attributes["task"] != newItem.attributes["task"] ||
        oldItem.attributes["checked"] != newItem.attributes["checked"]) {
      return {std::nullopt, QStringLiteral("Structural task-state mismatch")};
    }
  }
  auto expected = graftList(document, mapped->list, *parsed);
  auto result = apply(document, expected, list.range, newSource);
  if (!result.source && removedDefinition) {
    return {std::nullopt, QStringLiteral("Definition in use")};
  }
  return result;
}

EditResult setListKindSource(const Document& document, const SetListKind& edit) {
  auto mapped = mapList(document, edit.list);
  if (!mapped.list) {
    return {std::nullopt, mapped.error};
  }
  const auto& list = *mapped.list;
  const auto& node = document.nodes[edit.list];
  const auto ordered = node.type == MD_BLOCK_OL;
  if (ordered == edit.ordered &&
      (!edit.ordered || node.attributes["start"].toInt() == edit.start)) {
    return {document.source, {}};
  }
  if (edit.ordered) {
    if (edit.start < 0 || edit.start > 999999999 ||
        qint64(edit.start) + qint64(list.items.size()) - 1 > 999999999) {
      return {std::nullopt, QStringLiteral("Ordered list number overflow")};
    }
  }
  const auto marker = listMarkerAt(document.source, list.range.offset);
  if (!marker) {
    return {std::nullopt, QStringLiteral("Unsupported list marker")};
  }
  const auto delimiter = edit.ordered ? (ordered ? marker->delimiter : '.')
                                      : (ordered ? '-' : marker->delimiter);
  auto contents = std::vector<QByteArray>{};
  for (const auto& item : list.items) {
    contents.push_back(itemContent(document, item));
  }
  const auto gaps = itemGaps(document, list);
  const auto eol = listEol(document, list);
  const auto trailingEol = !listEndsAtEofWithoutEol(document, list);
  const auto newSource = buildListSource(
      contents, gaps, edit.ordered, edit.ordered ? edit.start : 0, delimiter,
      eol, trailingEol, list.continuationPrefix, list.blankLinePrefix);
  const auto canonicalSource = stripContainerPrefix(
      newSource, list.continuationPrefix, list.blankLinePrefix);
  const auto definitions = definitionInventory(document);
  QString contextError;
  auto parsed = parseListContext(document, definitions, list.range.offset,
                                 list.range.offset + list.range.size,
                                 canonicalSource, contextError);
  if (!parsed) {
    return {std::nullopt, contextError};
  }
  const auto& parsedList = parsed->document.nodes[parsed->root];
  if (parsedList.type != (edit.ordered ? MD_BLOCK_OL : MD_BLOCK_UL) ||
      parsedList.children.size() != list.items.size()) {
    return {std::nullopt, QStringLiteral("Structural list kind mismatch")};
  }
  if (edit.ordered &&
      parsedList.attributes["start"].toInt() != edit.start) {
    return {std::nullopt, QStringLiteral("Structural ordered list mismatch")};
  }
  if (parsedList.attributes["tight"].toBool() !=
      document.nodes[edit.list].attributes["tight"].toBool()) {
    return {std::nullopt, QStringLiteral("Structural tightness mismatch")};
  }
  for (size_t i = 0; i < list.items.size(); ++i) {
    const auto& oldItem = document.nodes[list.items[i].node];
    const auto& newItem =
        parsed->document.nodes[parsedList.children[i]];
    if (oldItem.attributes["task"] != newItem.attributes["task"] ||
        oldItem.attributes["checked"] != newItem.attributes["checked"]) {
      return {std::nullopt, QStringLiteral("Structural task-state mismatch")};
    }
  }
  auto expected = graftList(document, edit.list, *parsed);
  return apply(document, expected, list.range, newSource);
}

EditResult setItemTaskSource(const Document& document, const SetItemTask& edit) {
  QString error;
  auto mapped = mapItem(document, edit.item, error);
  if (!mapped) {
    return {std::nullopt, error};
  }
  const auto& node = document.nodes[edit.item];
  const bool isTask = node.attributes["task"].toBool();
  const bool checked = node.attributes["checked"].toBool();
  const bool desired = edit.state == TaskState::kChecked;
  if ((edit.state == TaskState::kNone && !isTask) ||
      (edit.state == TaskState::kUnchecked && isTask && !checked) ||
      (edit.state == TaskState::kChecked && isTask && checked)) {
    return {document.source, {}};
  }
  if ((edit.state == TaskState::kUnchecked ||
       edit.state == TaskState::kChecked) &&
      !taskEligible(document, edit.item)) {
    return {std::nullopt,
            QStringLiteral("Task state requires a plain text first paragraph")};
  }
  const auto& list = mapped->source;
  const auto& item = list.items[mapped->index];
  const auto marker = listMarkerAt(document.source, item.range.offset);
  if (!marker) {
    return {std::nullopt, QStringLiteral("Unsupported list marker")};
  }
  const auto markerPrefix =
      document.source.mid(item.range.offset, marker->content - item.range.offset);
  auto content = itemContent(document, item);
  const auto definitions = definitionInventory(document);
  qsizetype markerPosition = 0;
  bool prefix = false;
  if (isTask) {
    const auto markerPositionValue =
        contentOffsetFor(item, node.source ? node.source->offset : -1);
    if (!markerPositionValue || *markerPositionValue < 1) {
      return {std::nullopt, QStringLiteral("Task marker has no proven range")};
    }
    const auto markerBytes = content.mid(*markerPositionValue - 1, 4);
    if (markerBytes.size() != 4 || markerBytes[0] != '[' ||
        markerBytes[2] != ']' || markerBytes[3] != ' ' ||
        (markerBytes[1] != ' ' && markerBytes[1] != 'x' &&
         markerBytes[1] != 'X')) {
      return {std::nullopt, QStringLiteral("Task marker has no proven range")};
    }
    markerPosition = *markerPositionValue - 1;
    prefix = true;
  } else {
    const auto position =
        firstRenderedContentOffset(document, item, definitions);
    if (!position) {
      return {std::nullopt,
              QStringLiteral("Task state requires a plain text first paragraph")};
    }
    markerPosition = *position;
  }
  if (edit.state == TaskState::kNone) {
    if (!prefix) {
      return {document.source, {}};
    }
    content.remove(markerPosition, 4);
  } else if (prefix) {
    content[markerPosition + 1] = desired ? 'x' : ' ';
  } else {
    content.insert(markerPosition, desired ? "[x] " : "[ ] ");
  }
  const auto eol = listEol(document, list);
  const auto itemIsLast = mapped->index + 1 == list.items.size();
  const auto trailingEol =
      itemIsLast ? !listEndsAtEofWithoutEol(document, list) : true;
  const auto newItem = renderItem(markerPrefix, content, eol, trailingEol,
                                  list.continuationPrefix,
                                  list.blankLinePrefix);
  const auto itemEnd = item.range.offset + item.range.size;
  const auto listEnd = list.range.offset + list.range.size;
  const auto newSource =
      document.source.mid(list.range.offset,
                          item.range.offset - list.range.offset) +
      newItem + document.source.mid(itemEnd, listEnd - itemEnd);
  const auto canonicalSource = stripContainerPrefix(
      newSource, list.continuationPrefix, list.blankLinePrefix);
  QString contextError;
  auto parsed = parseListContext(document, definitions, list.range.offset,
                                 list.range.offset + list.range.size,
                                 canonicalSource, contextError);
  if (!parsed) {
    return {std::nullopt, contextError};
  }
  const auto& parsedList = parsed->document.nodes[parsed->root];
  if (mapped->index >= parsedList.children.size()) {
    return {std::nullopt, QStringLiteral("Task state is not representable")};
  }
  const auto& parsedItem =
      parsed->document.nodes[parsedList.children[mapped->index]];
  const auto newTask = parsedItem.attributes["task"].toBool();
  const auto newChecked = parsedItem.attributes["checked"].toBool();
  const auto validState =
      edit.state == TaskState::kNone
          ? !newTask
          : (edit.state == TaskState::kUnchecked ? newTask && !newChecked
                                                 : newTask && newChecked);
  if (!validState) {
    return {std::nullopt, QStringLiteral("Task state is not representable")};
  }
  if (parsedList.attributes["tight"].toBool() !=
      document.nodes[mapped->list].attributes["tight"].toBool()) {
    return {std::nullopt, QStringLiteral("Structural tightness mismatch")};
  }
  auto expected = graftList(document, mapped->list, *parsed);
  return apply(document, expected, item.range, newItem);
}

std::optional<SourceRange> mapFencedCodeRange(const Document& document,
                                              size_t node, QString& error) {
  const auto lines = listLines(document.source);
  for (size_t first = 0; first < lines.size(); ++first) {
    const auto& line = lines[first];
    auto position = line.start;
    while (position < line.bodyEnd && document.source[position] == ' ') {
      ++position;
    }
    if (position - line.start > 3 || position >= line.bodyEnd) {
      continue;
    }
    const auto marker = document.source[position];
    if (marker != '`' && marker != '~') {
      continue;
    }
    const auto markerStart = position;
    while (position < line.bodyEnd && document.source[position] == marker) {
      ++position;
    }
    const auto count = position - markerStart;
    if (count < 3 ||
        (marker == '`' &&
         document.source.mid(position, line.bodyEnd - position).contains('`'))) {
      continue;
    }
    auto end = document.source.size();
    for (size_t i = first + 1; i < lines.size(); ++i) {
      auto close = lines[i].start;
      while (close < lines[i].bodyEnd && document.source[close] == ' ') {
        ++close;
      }
      if (close - lines[i].start > 3 || close >= lines[i].bodyEnd ||
          document.source[close] != marker) {
        continue;
      }
      auto run = close;
      while (run < lines[i].bodyEnd && document.source[run] == marker) {
        ++run;
      }
      if (run - close < count) {
        continue;
      }
      while (run < lines[i].bodyEnd && space(document.source[run])) {
        ++run;
      }
      if (run == lines[i].bodyEnd) {
        end = lines[i].end;
        break;
      }
    }
    const SourceRange range{line.start, end - line.start};
    const auto isolated =
        parse(document.source.mid(range.offset, range.size));
    if (!isolated.document ||
        isolated.document->nodes[0].children.size() != 1) {
      continue;
    }
    const auto child = isolated.document->nodes[0].children.front();
    if (isolated.document->nodes[child].type != MD_BLOCK_CODE) {
      continue;
    }
    bool anchored = false;
    const bool empty =
        isolated.document->nodes[child].children.empty();
    if (!sameListSources(document, node, *isolated.document, child,
                         range.offset, anchored) ||
        (!anchored && !empty)) {
      continue;
    }
    return range;
  }
  error = QStringLiteral("Unsupported fenced code source map");
  return std::nullopt;
}

std::optional<FencedCodeBodySource> mapFencedCodeBodyInternal(
    const Document& document, size_t node, QString& error) {
  if (node >= document.nodes.size() ||
      document.nodes[node].kind != NodeKind::kBlock ||
      document.nodes[node].type != MD_BLOCK_CODE ||
      document.nodes[node].parent != std::optional<size_t>(0)) {
    error = QStringLiteral(
        "Fenced code body mapping supports only top-level code blocks");
    return std::nullopt;
  }
  QString rangeError;
  const auto blockRange = mapFencedCodeRange(document, node, rangeError);
  if (!blockRange) {
    error = rangeError;
    return std::nullopt;
  }
  const auto original =
      document.source.mid(blockRange->offset, blockRange->size);
  const auto lines = listLines(original);
  if (lines.empty()) {
    error = QStringLiteral("Fenced code has no source lines");
    return std::nullopt;
  }
  const auto& opening = lines.front();
  auto position = opening.start;
  while (position < opening.bodyEnd && original[position] == ' ') {
    ++position;
  }
  if (position - opening.start > 3 || position >= opening.bodyEnd) {
    error = QStringLiteral("Unsupported fenced code opening line");
    return std::nullopt;
  }
  const auto marker = original[position];
  if (marker != '`' && marker != '~') {
    error = QStringLiteral("Unsupported fenced code marker");
    return std::nullopt;
  }
  const auto markerStart = position;
  while (position < opening.bodyEnd && original[position] == marker) {
    ++position;
  }
  const auto markerCount = position - markerStart;
  if (markerCount < 3) {
    error = QStringLiteral("Unsupported fenced code marker");
    return std::nullopt;
  }
  size_t closingLine = lines.size();
  for (size_t i = 1; i < lines.size(); ++i) {
    auto close = lines[i].start;
    while (close < lines[i].bodyEnd && original[close] == ' ') {
      ++close;
    }
    if (close - lines[i].start > 3 || close >= lines[i].bodyEnd ||
        original[close] != marker) {
      continue;
    }
    auto run = close;
    while (run < lines[i].bodyEnd && original[run] == marker) {
      ++run;
    }
    if (run - close < markerCount) {
      continue;
    }
    while (run < lines[i].bodyEnd && space(original[run])) {
      ++run;
    }
    if (run == lines[i].bodyEnd) {
      closingLine = i;
      break;
    }
  }
  const auto bodyStart = opening.end;
  const auto bodyEnd =
      closingLine < lines.size() ? lines[closingLine].start : original.size();
  if (bodyStart > bodyEnd) {
    error = QStringLiteral("Invalid fenced code body range");
    return std::nullopt;
  }
  auto eol = original.mid(opening.bodyEnd, opening.end - opening.bodyEnd);
  if (eol.isEmpty()) {
    eol = documentEol(document.source);
  }
  return FencedCodeBodySource{
      *blockRange,
      {blockRange->offset + bodyStart, bodyEnd - bodyStart},
      eol,
      closingLine < lines.size()};
}

std::optional<SourceRange> mapTextBlockRange(const Document& document,
                                             size_t node, QString& error) {
  qsizetype first = document.source.size();
  qsizetype end = 0;
  if (!mappedTextBounds(document, node, first, end) || first >= end) {
    error = QStringLiteral("Block has no supported source range");
    return std::nullopt;
  }
  const auto start = first == 0 ? 0 : document.source.lastIndexOf('\n', first - 1) + 1;
  const auto newline = document.source.indexOf('\n', end - 1);
  const auto fullEnd = newline < 0 ? document.source.size() : newline + 1;
  const SourceRange range{start, fullEnd - start};
  const auto isolated =
      parse(document.source.mid(range.offset, range.size));
  if (!isolated.document ||
      isolated.document->nodes[0].children.size() != 1) {
    error = QStringLiteral("Block has no supported source range");
    return std::nullopt;
  }
  const auto child = isolated.document->nodes[0].children.front();
  if (isolated.document->nodes[child].type != document.nodes[node].type) {
    error = QStringLiteral("Block has no supported source range");
    return std::nullopt;
  }
  bool anchored = false;
  if (!sameListSources(document, node, *isolated.document, child,
                       range.offset, anchored) ||
      !anchored) {
    error = QStringLiteral("Block has no supported source range");
    return std::nullopt;
  }
  return range;
}

std::optional<SourceRange> mapTopLevelBlockRange(const Document& document,
                                                 size_t node, QString& error) {
  if (node >= document.nodes.size() ||
      document.nodes[node].kind != NodeKind::kBlock ||
      document.nodes[node].parent != std::optional<size_t>(0)) {
    error = QStringLiteral("Invalid block target");
    return std::nullopt;
  }
  const auto type = document.nodes[node].type;
  if (type == MD_BLOCK_P || type == MD_BLOCK_H) {
    for (const auto& block : mapBlocks(document)) {
      if (block.node == node) {
        return block.range;
      }
    }
    return mapTextBlockRange(document, node, error);
  }
  if (type == MD_BLOCK_UL || type == MD_BLOCK_OL) {
    const auto mapped = mapList(document, node);
    if (!mapped.list) {
      error = mapped.error;
      return std::nullopt;
    }
    return mapped.list->range;
  }
  if (type == MD_BLOCK_QUOTE) {
    const auto mapped = mapQuote(document, node);
    if (!mapped.quote) {
      error = mapped.error;
      return std::nullopt;
    }
    return mapped.quote->range;
  }
  if (type == MD_BLOCK_CODE) {
    return mapFencedCodeRange(document, node, error);
  }
  error = QStringLiteral("Unsupported block type for quote selection");
  return std::nullopt;
}

std::optional<SourceRange> mapNestedBlockRange(const Document& document,
                                               size_t node, QString& error) {
  if (node >= document.nodes.size() ||
      document.nodes[node].kind != NodeKind::kBlock ||
      !document.nodes[node].parent) {
    error = QStringLiteral("Invalid block target");
    return std::nullopt;
  }
  const auto parent = *document.nodes[node].parent;
  VirtualContent content;
  if (!buildVirtualContent(document, parent, content, error)) {
    return std::nullopt;
  }
  auto parsed = parseVirtual(document, content, error);
  if (!parsed) {
    return std::nullopt;
  }
  const auto position = childPosition(document, parent, node);
  if (!position ||
      *position >= parsed->document.nodes[0].children.size()) {
    error = QStringLiteral("Nested block context mismatch");
    return std::nullopt;
  }
  const auto virtualNode =
      parsed->document.nodes[0].children[*position];
  auto range = mapTopLevelBlockRange(parsed->document, virtualNode, error);
  if (!range) {
    return std::nullopt;
  }
  const auto start = virtualToReal(
      content, range->offset - parsed->contentOffset);
  const auto end = virtualToRealEnd(
      content, range->offset + range->size - parsed->contentOffset);
  return SourceRange{start, end - start};
}

QByteArray wrapSelectedLines(const QByteArray& source,
                             const QByteArray& continuationPrefix,
                             const QByteArray& blankPrefix) {
  QByteArray result;
  const auto lines = listLines(source);
  for (size_t i = 0; i < lines.size(); ++i) {
    const auto& line = lines[i];
    const auto body = source.mid(line.start, line.bodyEnd - line.start);
    const auto ending = source.mid(line.bodyEnd, line.end - line.bodyEnd);
    if (i == 0) {
      result += body.isEmpty() ? QByteArray(">") : QByteArray("> ") + body;
      result += ending;
      continue;
    }
    if (body.isEmpty()) {
      result += (blankPrefix.isEmpty() ? continuationPrefix : blankPrefix) + '>' +
                ending;
    } else if (body.startsWith(continuationPrefix)) {
      result += continuationPrefix + "> " +
                body.mid(continuationPrefix.size()) + ending;
    } else {
      result += "> " + body + ending;
    }
  }
  return result;
}

QByteArray unquoteNestedLines(const QByteArray& source,
                              const QByteArray& continuationPrefix,
                              const QByteArray& blankPrefix) {
  QByteArray result;
  const auto lines = listLines(source);
  for (size_t i = 0; i < lines.size(); ++i) {
    const auto& line = lines[i];
    const auto ending = source.mid(line.bodyEnd, line.end - line.bodyEnd);
    auto start = line.start;
    QByteArray parentPrefix;
    if (i > 0) {
      const auto body =
          source.mid(line.start, line.bodyEnd - line.start);
      if (body.startsWith(continuationPrefix)) {
        parentPrefix = continuationPrefix;
      } else if (body.startsWith(blankPrefix)) {
        parentPrefix = blankPrefix;
      }
      start += parentPrefix.size();
    }
    const ListLine view{start, line.bodyEnd, line.end};
    const auto contentStart = quotePrefixEnd(source, view);
    if (!contentStart) {
      result += source.mid(line.start, line.bodyEnd - line.start) + ending;
      continue;
    }
    const auto content = source.mid(*contentStart,
                                    line.bodyEnd - *contentStart);
    if (content.trimmed().isEmpty()) {
      result += blankPrefix + ending;
    } else {
      result += parentPrefix + content + ending;
    }
  }
  return result;
}

EditResult wrapInQuoteSource(const Document& document,
                             const WrapInQuote& edit) {
  if (document.nodes.empty() || edit.parent >= document.nodes.size() ||
      document.nodes[edit.parent].kind != NodeKind::kBlock ||
      (document.nodes[edit.parent].type != MD_BLOCK_DOC &&
       document.nodes[edit.parent].type != MD_BLOCK_QUOTE &&
       document.nodes[edit.parent].type != MD_BLOCK_LI)) {
    return {std::nullopt, QStringLiteral("Invalid quote parent")};
  }
  const auto& parent = document.nodes[edit.parent];
  if (edit.count == 0 || edit.first >= parent.children.size() ||
      edit.count > parent.children.size() - edit.first) {
    return {std::nullopt, QStringLiteral("Invalid quote selection")};
  }
  QByteArray continuationPrefix;
  QByteArray blankPrefix;
  if (edit.parent != 0) {
    QString error;
    VirtualContent content;
    if (!buildVirtualContent(document, edit.parent, content, error)) {
      return {std::nullopt, error};
    }
    continuationPrefix = content.continuationPrefix;
    blankPrefix = content.blankLinePrefix;
  }
  std::vector<SourceRange> ranges;
  for (size_t i = 0; i < edit.count; ++i) {
    const auto child = parent.children[edit.first + i];
    if (child >= document.nodes.size() ||
        document.nodes[child].kind != NodeKind::kBlock) {
      return {std::nullopt, QStringLiteral("Invalid quote selection")};
    }
    QString error;
    const auto range = edit.parent == 0
                           ? mapTopLevelBlockRange(document, child, error)
                           : mapNestedBlockRange(document, child, error);
    if (!range) {
      return {std::nullopt, error};
    }
    ranges.push_back(*range);
  }
  const auto offset = ranges.front().offset;
  const auto end = ranges.back().offset + ranges.back().size;
  for (const auto& range : ranges) {
    if (range.offset < offset || range.offset + range.size > end) {
      return {std::nullopt, QStringLiteral("Invalid quote selection")};
    }
  }
  const SourceRange patch{offset, end - offset};
  const auto replacement = wrapSelectedLines(
      document.source.mid(patch.offset, patch.size), continuationPrefix,
      blankPrefix);

  auto expected = document;
  const auto quoteIndex = expected.nodes.size();
  expected.nodes.push_back(
      {NodeKind::kBlock, MD_BLOCK_QUOTE, {}, {}, {}, std::nullopt, edit.parent});
  for (size_t i = 0; i < edit.count; ++i) {
    const auto child = document.nodes[edit.parent].children[edit.first + i];
    expected.nodes[child].parent = quoteIndex;
    expected.nodes[quoteIndex].children.push_back(child);
  }
  auto& children = expected.nodes[edit.parent].children;
  children.erase(children.begin() + qsizetype(edit.first),
                 children.begin() + qsizetype(edit.first + edit.count));
  children.insert(children.begin() + qsizetype(edit.first), quoteIndex);
  if (document.nodes[edit.parent].type == MD_BLOCK_LI) {
    updateLiTightness(expected, edit.parent);
  }
  return apply(document, expected, patch, replacement);
}

EditResult unwrapQuoteSource(const Document& document,
                             const UnwrapQuote& edit) {
  if (edit.quote >= document.nodes.size() ||
      document.nodes[edit.quote].kind != NodeKind::kBlock ||
      document.nodes[edit.quote].type != MD_BLOCK_QUOTE) {
    return {std::nullopt, QStringLiteral("Invalid quote target")};
  }
  const auto mapped = mapQuote(document, edit.quote);
  if (!mapped.quote) {
    return {std::nullopt, mapped.error};
  }
  const auto replacement = unquoteNestedLines(
      document.source.mid(mapped.quote->range.offset, mapped.quote->range.size),
      mapped.quote->continuationPrefix, mapped.quote->blankLinePrefix);
  auto expected = document;
  const auto parent = document.nodes[edit.quote].parent.value_or(0);
  auto& children = expected.nodes[parent].children;
  const auto position =
      std::find(children.begin(), children.end(), edit.quote);
  if (position == children.end()) {
    return {std::nullopt, QStringLiteral("Invalid quote target")};
  }
  std::vector<size_t> lifted = expected.nodes[edit.quote].children;
  for (auto child : lifted) {
    expected.nodes[child].parent = parent;
  }
  const auto index = position - children.begin();
  children.erase(position);
  children.insert(children.begin() + index, lifted.begin(), lifted.end());
  if (document.nodes[parent].type == MD_BLOCK_LI) {
    updateLiTightness(expected, parent);
  }
  return apply(document, expected, mapped.quote->range, replacement);
}

EditResult wrapInListSource(const Document& document, const WrapInList& edit) {
  if (document.nodes.empty() || edit.parent != 0 ||
      document.nodes[0].kind != NodeKind::kBlock ||
      document.nodes[0].type != MD_BLOCK_DOC) {
    return {std::nullopt,
            QStringLiteral("List creation requires the document root")};
  }
  const auto& root = document.nodes[0];
  if (edit.first >= root.children.size()) {
    return {std::nullopt, QStringLiteral("Invalid list selection")};
  }
  const auto block = root.children[edit.first];
  if (block >= document.nodes.size() ||
      document.nodes[block].kind != NodeKind::kBlock ||
      document.nodes[block].type != MD_BLOCK_P) {
    return {std::nullopt,
            QStringLiteral("List creation requires a paragraph")};
  }
  if (edit.ordered && (edit.start < 1 || edit.start > 999999999)) {
    return {std::nullopt, QStringLiteral("Invalid ordered list start")};
  }
  qsizetype first = document.source.size();
  qsizetype end = 0;
  if (!mappedTextBounds(document, block, first, end) || first >= end) {
    return {std::nullopt,
            QStringLiteral("Block has no supported source range")};
  }
  const auto start =
      first == 0 ? 0 : document.source.lastIndexOf('\n', first - 1) + 1;
  const auto newline = document.source.indexOf('\n', end - 1);
  const auto fullEnd = newline < 0 ? document.source.size() : newline + 1;
  const SourceRange range{start, fullEnd - start};
  const auto original = document.source.mid(range.offset, range.size);
  const auto lines = listLines(original);
  if (lines.empty()) {
    return {std::nullopt, QStringLiteral("Paragraph has no source lines")};
  }
  const auto marker = edit.ordered
                          ? QByteArray::number(edit.start) + QByteArray(". ")
                          : QByteArray("- ");
  const QByteArray continuation(marker.size(), ' ');
  QByteArray replacement;
  for (size_t i = 0; i < lines.size(); ++i) {
    const auto& line = lines[i];
    const auto body = original.mid(line.start, line.bodyEnd - line.start);
    const auto ending = original.mid(line.bodyEnd, line.end - line.bodyEnd);
    if (body.isEmpty() || body.startsWith(' ') || body.startsWith('\t')) {
      return {std::nullopt,
              QStringLiteral("List creation requires an unindented paragraph")};
    }
    replacement += i == 0 ? marker : continuation;
    replacement += body;
    replacement += ending;
  }
  const auto definitions = definitionInventory(document);
  QString contextError;
  auto parsed = parseListContext(document, definitions, range.offset,
                                 range.offset + range.size, replacement,
                                 contextError);
  if (!parsed) {
    return {std::nullopt, contextError};
  }
  const auto& list = parsed->document.nodes[parsed->root];
  const auto expectedType = edit.ordered ? MD_BLOCK_OL : MD_BLOCK_UL;
  if (list.kind != NodeKind::kBlock || list.type != expectedType ||
      list.children.size() != 1) {
    return {std::nullopt, QStringLiteral("Structural list mismatch")};
  }
  const auto itemIndex = list.children.front();
  if (itemIndex >= parsed->document.nodes.size() ||
      parsed->document.nodes[itemIndex].kind != NodeKind::kBlock ||
      parsed->document.nodes[itemIndex].type != MD_BLOCK_LI) {
    return {std::nullopt, QStringLiteral("Structural list item mismatch")};
  }
  if (edit.ordered &&
      list.attributes["start"].toInt() != edit.start) {
    return {std::nullopt,
            QStringLiteral("Structural ordered list start mismatch")};
  }
  auto expected = document;
  auto& siblings = expected.nodes[0].children;
  const auto position = std::find(siblings.begin(), siblings.end(), block);
  if (position == siblings.end()) {
    return {std::nullopt, QStringLiteral("Invalid list selection")};
  }
  const auto newIndex =
      copySubtree(expected, parsed->document, parsed->root, 0);
  *position = newIndex;
  return apply(document, expected, range, replacement);
}

EditResult replaceInlineCodeSource(const Document& document,
                                   const ReplaceInlineCode& edit) {
  if (edit.node >= document.nodes.size()) {
    return {std::nullopt, QStringLiteral("Invalid inline code target")};
  }
  const auto& node = document.nodes[edit.node];
  if (node.kind != NodeKind::kText || node.type != MD_TEXT_CODE ||
      !node.source || !node.parent ||
      *node.parent >= document.nodes.size()) {
    return {std::nullopt, QStringLiteral("Invalid inline code target")};
  }
  const auto& parent = document.nodes[*node.parent];
  if (parent.kind != NodeKind::kSpan || parent.type != MD_SPAN_CODE ||
      parent.children.size() != 1 || parent.children.front() != edit.node ||
      node.source->offset < 0 ||
      node.source->offset > document.source.size() ||
      node.source->size != node.text.size() ||
      node.source->size > document.source.size() - node.source->offset ||
      document.source.mid(node.source->offset, node.source->size) !=
          node.text) {
    return {std::nullopt, QStringLiteral("Invalid inline code context")};
  }
  if (edit.text.isEmpty() || !validUtf8(edit.text) ||
      edit.text.contains('\0') || edit.text.contains('\r') ||
      edit.text.contains('\n')) {
    return {std::nullopt,
            QStringLiteral("Inline code text must be nonempty single-line UTF-8")};
  }
  if (edit.text == node.text) {
    return {document.source, {}};
  }
  auto expected = document;
  expected.nodes[edit.node].text = edit.text;
  return apply(document, expected, *node.source, edit.text);
}

EditResult replaceCodeBodySource(const Document& document,
                                 const ReplaceCodeBody& edit) {
  if (edit.node >= document.nodes.size() ||
      document.nodes[edit.node].kind != NodeKind::kBlock ||
      document.nodes[edit.node].type != MD_BLOCK_CODE) {
    return {std::nullopt, QStringLiteral("Invalid fenced code target")};
  }
  QString mapError;
  const auto mapped = mapFencedCodeBodyInternal(document, edit.node, mapError);
  if (!mapped) {
    return {std::nullopt, mapError};
  }
  if (!validUtf8(edit.body) || edit.body.contains('\0')) {
    return {std::nullopt, QStringLiteral("Invalid fenced code body")};
  }
  QByteArray normalized = edit.body;
  normalized.replace("\r\n", "\n");
  normalized.replace('\r', '\n');
  if (!normalized.isEmpty()) {
    if (!normalized.endsWith('\n')) {
      normalized += '\n';
    }
    if (mapped->eol != "\n") {
      normalized.replace('\n', mapped->eol);
    }
  }
  const auto originalBody =
      document.source.mid(mapped->body.offset, mapped->body.size);
  if (normalized == originalBody) {
    return {document.source, {}};
  }
  const auto blockEnd = mapped->block.offset + mapped->block.size;
  const auto suffixStart = mapped->body.offset + mapped->body.size;
  if (suffixStart < mapped->body.offset || suffixStart > blockEnd) {
    return {std::nullopt, QStringLiteral("Invalid fenced code body range")};
  }
  const auto candidate =
      document.source.mid(mapped->block.offset,
                          mapped->body.offset - mapped->block.offset) +
      normalized +
      document.source.mid(suffixStart, blockEnd - suffixStart);
  auto parsed = parse(candidate);
  if (!parsed.document || parsed.document->nodes[0].children.size() != 1) {
    return {std::nullopt, QStringLiteral("Structural fenced code mismatch")};
  }
  const auto child = parsed.document->nodes[0].children.front();
  if (parsed.document->nodes[child].type != MD_BLOCK_CODE ||
      parsed.document->nodes[child].attributes !=
          document.nodes[edit.node].attributes) {
    return {std::nullopt, QStringLiteral("Structural fenced code mismatch")};
  }
  auto expected = document;
  auto& siblings = expected.nodes[0].children;
  const auto position =
      std::find(siblings.begin(), siblings.end(), edit.node);
  if (position == siblings.end()) {
    return {std::nullopt, QStringLiteral("Invalid fenced code target")};
  }
  const auto newIndex = copySubtree(expected, *parsed.document, child, 0);
  *position = newIndex;
  return apply(document, expected, mapped->body, normalized);
}

std::optional<SourceRange> contentRangeFor(const Document& document,
                                           const ListItemSource& item,
                                           SourceRange real) {
  Q_UNUSED(document);
  if (real.size <= 0) {
    return std::nullopt;
  }
  const auto start = contentOffsetFor(item, real.offset);
  const auto end = contentOffsetFor(item, real.offset + real.size - 1);
  if (!start || !end || *end < *start) {
    return std::nullopt;
  }
  return SourceRange{*start, *end + 1 - *start};
}

QByteArray replaceContentRange(const QByteArray& content, SourceRange range,
                               const QByteArray& replacement) {
  auto result = content;
  result.replace(range.offset, range.size, replacement);
  return result;
}

void removeGapForItem(std::vector<QByteArray>& gaps, size_t index,
                      size_t count) {
  if (gaps.empty()) {
    return;
  }
  if (index + 1 >= count) {
    gaps.erase(gaps.begin() + qsizetype(gaps.size()) - 1);
  } else {
    gaps.erase(gaps.begin() + qsizetype(index));
  }
}

EditResult indentListItemSource(const Document& document,
                                const IndentListItem& edit) {
  QString error;
  auto mapped = mapItem(document, edit.item, error);
  if (!mapped) {
    return {std::nullopt, error};
  }
  const auto& source = mapped->source;
  const auto index = mapped->index;
  if (index == 0) {
    return {std::nullopt, QStringLiteral("Invalid target: no previous list item")};
  }
  const auto& listNode = document.nodes[mapped->list];
  const auto ordered = listNode.type == MD_BLOCK_OL;
  const auto start = ordered ? listNode.attributes["start"].toInt() : 0;
  const auto marker = listMarkerAt(document.source, source.range.offset);
  if (!marker) {
    return {std::nullopt, QStringLiteral("Unsupported list marker")};
  }
  const auto eol = listEol(document, source);
  const auto previousIndex = index - 1;
  const auto previousNode = source.items[previousIndex].node;
  auto previousContent = itemContent(document, source.items[previousIndex]);
  const auto movedContent = itemContent(document, source.items[index]);

  size_t targetList = document.nodes.size();
  for (auto child : document.nodes[previousNode].children) {
    if (document.nodes[child].kind == NodeKind::kBlock &&
        document.nodes[child].type == listNode.type) {
      targetList = child;
    }
  }

  if (targetList < document.nodes.size()) {
    const auto target = mapList(document, targetList);
    if (!target.list) {
      return {std::nullopt, target.error};
    }
    auto contents = std::vector<QByteArray>{};
    for (const auto& item : target.list->items) {
      contents.push_back(itemContent(document, item));
    }
    auto gaps = itemGaps(document, *target.list);
    const auto targetOrdered =
        document.nodes[targetList].type == MD_BLOCK_OL;
    const auto targetStart =
        targetOrdered
            ? document.nodes[targetList].attributes["start"].toInt()
            : 0;
    if (targetOrdered &&
        qint64(targetStart) + qint64(contents.size()) > 999999999) {
      return {std::nullopt, QStringLiteral("Ordered list number overflow")};
    }
    const auto targetMarker =
        listMarkerAt(document.source, target.list->range.offset);
    if (!targetMarker) {
      return {std::nullopt, QStringLiteral("Unsupported list marker")};
    }
    const auto gap =
        gapFor(!document.nodes[targetList].attributes["tight"].toBool(),
               target.list->continuationPrefix, target.list->blankLinePrefix,
               eol);
    contents.push_back(movedContent);
    gaps.push_back(gap);
    const auto targetReal = buildListSource(
        contents, gaps, targetOrdered, targetStart, targetMarker->delimiter,
        eol, !listEndsAtEofWithoutEol(document, *target.list),
        target.list->continuationPrefix, target.list->blankLinePrefix);
    const auto targetSource = stripContainerPrefix(
        targetReal, target.list->continuationPrefix,
        target.list->blankLinePrefix);
    const auto range =
        contentRangeFor(document, source.items[previousIndex],
                        target.list->range);
    if (!range) {
      return {std::nullopt, QStringLiteral("Unsupported nested list range")};
    }
    previousContent = replaceContentRange(previousContent, *range, targetSource);
  } else {
    const auto newListSource = buildListSource(
        {movedContent}, {}, ordered, ordered ? 1 : 0, marker->delimiter, eol,
        true, {}, {});
    if (!previousContent.isEmpty() && !previousContent.endsWith('\n') &&
        !previousContent.endsWith('\r')) {
      previousContent += eol;
    }
    previousContent += newListSource;
  }

  auto contents = std::vector<QByteArray>{};
  for (size_t i = 0; i < source.items.size(); ++i) {
    if (i == index) {
      continue;
    }
    contents.push_back(i == previousIndex
                           ? previousContent
                           : itemContent(document, source.items[i]));
  }
  auto gaps = itemGaps(document, source);
  removeGapForItem(gaps, index, source.items.size());
  const auto trailingEol = !listEndsAtEofWithoutEol(document, source);
  const auto newSource = buildListSource(
      contents, gaps, ordered, start, marker->delimiter, eol, trailingEol,
      source.continuationPrefix, source.blankLinePrefix);
  const auto canonicalSource = stripContainerPrefix(
      newSource, source.continuationPrefix, source.blankLinePrefix);
  const auto definitions = definitionInventory(document);
  QString contextError;
  auto parsed = parseListContext(document, definitions, source.range.offset,
                                 source.range.offset + source.range.size,
                                 canonicalSource, contextError);
  if (!parsed) {
    return {std::nullopt, contextError};
  }
  const auto& parsedList = parsed->document.nodes[parsed->root];
  if (parsedList.children.size() != source.items.size() - 1) {
    return {std::nullopt, QStringLiteral("Structural list mismatch")};
  }
  auto expected = graftList(document, mapped->list, *parsed);
  return apply(document, expected, source.range, newSource);
}

bool validTableCellPayload(const QByteArray& value, QString& error) {
  if (value.contains('\0') || value.contains('\r') || value.contains('\n') ||
      !validUtf8(value)) {
    error = QStringLiteral("Invalid table cell payload");
    return false;
  }
  if (!value.isEmpty()) {
    if (value.front() == ' ' || value.back() == ' ' ||
        value.contains('\t')) {
      error = QStringLiteral("Invalid table cell whitespace");
      return false;
    }
  }
  return true;
}

QByteArray escapeTableCell(const QByteArray& value) {
  constexpr auto kPunctuation = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
  QByteArray escaped;
  for (auto character : value) {
    if (QByteArrayView(kPunctuation).contains(character)) {
      escaped += '\\';
    }
    escaped += character;
  }
  return escaped;
}

QByteArray tableAlignmentDelimiter(TableAlignment alignment) {
  switch (alignment) {
    case TableAlignment::kLeft:
      return ":---";
    case TableAlignment::kCenter:
      return ":---:";
    case TableAlignment::kRight:
      return "---:";
    case TableAlignment::kNone:
      break;
  }
  return "---";
}

QByteArray serializeTable(
    const std::vector<QByteArray>& header,
    const std::vector<std::vector<QByteArray>>& body,
    const std::vector<TableAlignment>& alignments, const QByteArray& eol,
    bool trailingEol, const QByteArray& continuationPrefix = {}) {
  QByteArray result;
  bool firstRow = true;
  const auto row = [&](const std::vector<QByteArray>& cells) {
    QByteArray line;
    if (!firstRow) {
      line += continuationPrefix;
    }
    firstRow = false;
    line += "|";
    for (const auto& cell : cells) {
      line += " ";
      line += cell;
      line += " |";
    }
    line += eol;
    return line;
  };
  result += row(header);
  std::vector<QByteArray> delimiter;
  delimiter.reserve(alignments.size());
  for (const auto alignment : alignments) {
    delimiter.push_back(tableAlignmentDelimiter(alignment));
  }
  result += row(delimiter);
  for (const auto& cells : body) {
    result += row(cells);
  }
  if (!trailingEol && result.endsWith(eol)) {
    result.chop(eol.size());
  }
  return result;
}

std::optional<ContextList> parseTableContext(
    const Document& document, const std::vector<DefinitionCandidate>& definitions,
    qsizetype first, qsizetype last, const QByteArray& tableSource,
    QString& error) {
  qsizetype offset = 0;
  const auto context = definitionContext(document, definitions, first, last,
                                         offset);
  const auto header = context.left(offset);
  QByteArray suffix;
  if (context.size() > offset + (last - first)) {
    suffix = context.mid(offset + (last - first));
  }
  auto parsed = parse(header + tableSource + suffix);
  if (!parsed.document) {
    error = QStringLiteral("Structural table parse failed");
    return std::nullopt;
  }
  size_t root = parsed.document->nodes.size();
  for (auto child : parsed.document->nodes[0].children) {
    if (parsed.document->nodes[child].type == MD_BLOCK_TABLE) {
      root = child;
      break;
    }
  }
  if (root >= parsed.document->nodes.size()) {
    error = QStringLiteral("Structural table parse produced no table");
    return std::nullopt;
  }
  return ContextList{std::move(*parsed.document), root};
}

QByteArray tableEol(const Document& document, const TableSource& table) {
  return documentEol(
      document.source.mid(table.range.offset, table.range.size));
}

bool tableTrailingEol(const Document& document, const TableSource& table) {
  return !(table.range.offset + table.range.size == document.source.size() &&
           !document.source.endsWith('\n') &&
           !document.source.endsWith('\r'));
}

std::vector<QByteArray> tableHeaderFragments(const Document& document,
                                             const TableSource& table) {
  std::vector<QByteArray> result;
  for (const auto& cell : table.header.cells) {
    result.push_back(document.source.mid(cell.range.offset, cell.range.size));
  }
  return result;
}

std::vector<std::vector<QByteArray>> tableBodyFragments(
    const Document& document, const TableSource& table) {
  std::vector<std::vector<QByteArray>> result;
  for (const auto& row : table.body) {
    std::vector<QByteArray> cells;
    for (const auto& cell : row.cells) {
      cells.push_back(document.source.mid(cell.range.offset, cell.range.size));
    }
    result.push_back(std::move(cells));
  }
  return result;
}

EditResult insertTableRowSource(const Document& document,
                                const InsertTableRow& edit) {
  const auto mapped = mapTable(document, edit.table);
  if (!mapped.table) {
    return {std::nullopt, mapped.error};
  }
  const auto& table = *mapped.table;
  if (edit.position > table.body.size()) {
    return {std::nullopt, QStringLiteral("Invalid table row position")};
  }
  if (edit.cells.size() != table.columns) {
    return {std::nullopt, QStringLiteral("Invalid table row payload")};
  }
  std::vector<QByteArray> newCells;
  newCells.reserve(edit.cells.size());
  for (const auto& cell : edit.cells) {
    QString error;
    if (!validTableCellPayload(cell, error)) {
      return {std::nullopt, error};
    }
    newCells.push_back(escapeTableCell(cell));
  }
  auto body = tableBodyFragments(document, table);
  body.insert(body.begin() + qsizetype(edit.position), std::move(newCells));
  const auto header = tableHeaderFragments(document, table);
  const auto eol = tableEol(document, table);
  const auto newSource = serializeTable(header, body, table.alignments, eol,
                                        tableTrailingEol(document, table), table.continuationPrefix);
  const auto canonicalSource = stripContainerPrefix(
      newSource, table.continuationPrefix, table.blankLinePrefix);
  const auto definitions = definitionInventory(document);
  QString contextError;
  auto parsed = parseTableContext(document, definitions, table.range.offset,
                                 table.range.offset + table.range.size,
                                 canonicalSource, contextError);
  if (!parsed) {
    return {std::nullopt, contextError};
  }
  const auto& parsedTable = parsed->document.nodes[parsed->root];
  if (parsedTable.attributes["body_rows"].toInt() != int(body.size()) ||
      parsedTable.attributes["columns"].toInt() != int(table.columns)) {
    return {std::nullopt, QStringLiteral("Structural table mismatch")};
  }
  auto expected = graftList(document, edit.table, *parsed);
  return apply(document, expected, table.range, newSource);
}

EditResult removeTableRowSource(const Document& document,
                                const RemoveTableRow& edit) {
  const auto mapped = mapTable(document, edit.table);
  if (!mapped.table) {
    return {std::nullopt, mapped.error};
  }
  const auto& table = *mapped.table;
  if (table.body.empty() || edit.index >= table.body.size()) {
    return {std::nullopt, QStringLiteral("Invalid table row index")};
  }
  auto body = tableBodyFragments(document, table);
  body.erase(body.begin() + qsizetype(edit.index));
  const auto header = tableHeaderFragments(document, table);
  const auto eol = tableEol(document, table);
  const auto newSource = serializeTable(header, body, table.alignments, eol,
                                        tableTrailingEol(document, table), table.continuationPrefix);
  const auto canonicalSource = stripContainerPrefix(
      newSource, table.continuationPrefix, table.blankLinePrefix);
  const auto definitions = definitionInventory(document);
  QString contextError;
  auto parsed = parseTableContext(document, definitions, table.range.offset,
                                 table.range.offset + table.range.size,
                                 canonicalSource, contextError);
  if (!parsed) {
    return {std::nullopt, contextError};
  }
  const auto& parsedTable = parsed->document.nodes[parsed->root];
  if (parsedTable.attributes["body_rows"].toInt() != int(body.size())) {
    return {std::nullopt, QStringLiteral("Structural table mismatch")};
  }
  auto expected = graftList(document, edit.table, *parsed);
  return apply(document, expected, table.range, newSource);
}

EditResult setTableAlignmentSource(const Document& document,
                                   const SetTableAlignment& edit) {
  const auto mapped = mapTable(document, edit.table);
  if (!mapped.table) {
    return {std::nullopt, mapped.error};
  }
  const auto& table = *mapped.table;
  if (edit.column >= table.columns) {
    return {std::nullopt, QStringLiteral("Invalid table column index")};
  }
  if (table.alignments[edit.column] == edit.alignment) {
    return {document.source, {}};
  }
  auto alignments = table.alignments;
  alignments[edit.column] = edit.alignment;
  const auto header = tableHeaderFragments(document, table);
  const auto body = tableBodyFragments(document, table);
  const auto eol = tableEol(document, table);
  const auto newSource = serializeTable(header, body, alignments, eol,
                                        tableTrailingEol(document, table), table.continuationPrefix);
  const auto canonicalSource = stripContainerPrefix(
      newSource, table.continuationPrefix, table.blankLinePrefix);
  const auto definitions = definitionInventory(document);
  QString contextError;
  auto parsed = parseTableContext(document, definitions, table.range.offset,
                                 table.range.offset + table.range.size,
                                 canonicalSource, contextError);
  if (!parsed) {
    return {std::nullopt, contextError};
  }
  const auto& parsedTable = parsed->document.nodes[parsed->root];
  if (parsedTable.attributes["columns"].toInt() != int(table.columns)) {
    return {std::nullopt, QStringLiteral("Structural table mismatch")};
  }
  auto expected = graftList(document, edit.table, *parsed);
  return apply(document, expected, table.range, newSource);
}

EditResult insertTableColumnSource(const Document& document,
                                   const InsertTableColumn& edit) {
  const auto mapped = mapTable(document, edit.table);
  if (!mapped.table) {
    return {std::nullopt, mapped.error};
  }
  const auto& table = *mapped.table;
  if (edit.position > table.columns) {
    return {std::nullopt, QStringLiteral("Invalid table column position")};
  }
  if (edit.bodyCells.size() != table.body.size()) {
    return {std::nullopt, QStringLiteral("Invalid table column payload")};
  }
  QString error;
  if (!validTableCellPayload(edit.header, error)) {
    return {std::nullopt, error};
  }
  std::vector<QByteArray> newBodyCells;
  newBodyCells.reserve(edit.bodyCells.size());
  for (const auto& cell : edit.bodyCells) {
    if (!validTableCellPayload(cell, error)) {
      return {std::nullopt, error};
    }
    newBodyCells.push_back(escapeTableCell(cell));
  }
  auto header = tableHeaderFragments(document, table);
  header.insert(header.begin() + qsizetype(edit.position),
                escapeTableCell(edit.header));
  auto body = tableBodyFragments(document, table);
  for (size_t row = 0; row < body.size(); ++row) {
    body[row].insert(body[row].begin() + qsizetype(edit.position),
                     newBodyCells[row]);
  }
  auto alignments = table.alignments;
  alignments.insert(alignments.begin() + qsizetype(edit.position),
                    edit.alignment);
  const auto eol = tableEol(document, table);
  const auto newSource = serializeTable(header, body, alignments, eol,
                                        tableTrailingEol(document, table), table.continuationPrefix);
  const auto canonicalSource = stripContainerPrefix(
      newSource, table.continuationPrefix, table.blankLinePrefix);
  const auto definitions = definitionInventory(document);
  QString contextError;
  auto parsed = parseTableContext(document, definitions, table.range.offset,
                                 table.range.offset + table.range.size,
                                 canonicalSource, contextError);
  if (!parsed) {
    return {std::nullopt, contextError};
  }
  const auto& parsedTable = parsed->document.nodes[parsed->root];
  if (parsedTable.attributes["columns"].toInt() != int(table.columns + 1) ||
      parsedTable.attributes["body_rows"].toInt() != int(body.size())) {
    return {std::nullopt, QStringLiteral("Structural table mismatch")};
  }
  auto expected = graftList(document, edit.table, *parsed);
  return apply(document, expected, table.range, newSource);
}

EditResult removeTableColumnSource(const Document& document,
                                   const RemoveTableColumn& edit) {
  const auto mapped = mapTable(document, edit.table);
  if (!mapped.table) {
    return {std::nullopt, mapped.error};
  }
  const auto& table = *mapped.table;
  if (table.columns <= 1) {
    return {std::nullopt, QStringLiteral("Cannot remove the last table column")};
  }
  if (edit.index >= table.columns) {
    return {std::nullopt, QStringLiteral("Invalid table column index")};
  }
  auto header = tableHeaderFragments(document, table);
  header.erase(header.begin() + qsizetype(edit.index));
  auto body = tableBodyFragments(document, table);
  for (auto& row : body) {
    row.erase(row.begin() + qsizetype(edit.index));
  }
  auto alignments = table.alignments;
  alignments.erase(alignments.begin() + qsizetype(edit.index));
  const auto eol = tableEol(document, table);
  const auto newSource = serializeTable(header, body, alignments, eol,
                                        tableTrailingEol(document, table), table.continuationPrefix);
  const auto canonicalSource = stripContainerPrefix(
      newSource, table.continuationPrefix, table.blankLinePrefix);
  const auto definitions = definitionInventory(document);
  QString contextError;
  auto parsed = parseTableContext(document, definitions, table.range.offset,
                                 table.range.offset + table.range.size,
                                 canonicalSource, contextError);
  if (!parsed) {
    return {std::nullopt, contextError};
  }
  const auto& parsedTable = parsed->document.nodes[parsed->root];
  if (parsedTable.attributes["columns"].toInt() != int(table.columns - 1) ||
      parsedTable.attributes["body_rows"].toInt() != int(body.size())) {
    return {std::nullopt, QStringLiteral("Structural table mismatch")};
  }
  auto expected = graftList(document, edit.table, *parsed);
  return apply(document, expected, table.range, newSource);
}

EditResult removeTableSource(const Document& document, const RemoveTable& edit) {
  const auto mapped = mapTable(document, edit.table);
  if (!mapped.table) {
    return {std::nullopt, mapped.error};
  }
  const auto parent = document.nodes[edit.table].parent;
  SourceRange removal = mapped.table->range;
  if (parent && document.nodes[*parent].type == MD_BLOCK_LI) {
    size_t firstBlock = document.nodes[*parent].children.size();
    for (auto child : document.nodes[*parent].children) {
      if (document.nodes[child].kind == NodeKind::kBlock) {
        firstBlock = child;
        break;
      }
    }
    size_t blocks = 0;
    for (auto child : document.nodes[*parent].children) {
      if (document.nodes[child].kind == NodeKind::kBlock) {
        ++blocks;
      }
    }
    if (firstBlock == edit.table) {
      const auto lineStart = lineStartAt(document.source, mapped.table->range.offset);
      bool onlySpaces = true;
      for (auto i = lineStart; i < mapped.table->range.offset; ++i) {
        if (document.source[i] != ' ') {
          onlySpaces = false;
          break;
        }
      }
      if (!onlySpaces) {
        return {std::nullopt,
                QStringLiteral("Unsupported RemoveTable on an LI marker line")};
      }
    }
    if (blocks <= 1) {
      return {std::nullopt,
              QStringLiteral("Unsupported cascade: table is the only LI block")};
    }
    removal = {lineStartAt(document.source, mapped.table->range.offset),
               mapped.table->range.offset + mapped.table->range.size -
                   lineStartAt(document.source, mapped.table->range.offset)};
  } else if (parent && document.nodes[*parent].type == MD_BLOCK_QUOTE) {
    if (document.nodes[*parent].children.size() <= 1) {
      return {std::nullopt,
              QStringLiteral("Unsupported cascade: table is the only quote block")};
    }
    const auto lineStart = lineStartAt(document.source, mapped.table->range.offset);
    removal = {lineStart, mapped.table->range.offset +
                              mapped.table->range.size - lineStart};
  }
  auto expected = removeListNode(document, edit.table);
  if (parent && document.nodes[*parent].type == MD_BLOCK_LI) {
    updateLiTightness(expected, *parent);
  }
  return apply(document, expected, removal, QByteArray{});
}

std::optional<size_t> tableNodeForCell(const Document& document, size_t cell) {
  auto parent = document.nodes[cell].parent;
  while (parent) {
    if (*parent >= document.nodes.size()) {
      return std::nullopt;
    }
    if (document.nodes[*parent].kind == NodeKind::kBlock &&
        document.nodes[*parent].type == MD_BLOCK_TABLE) {
      return *parent;
    }
    parent = document.nodes[*parent].parent;
  }
  return std::nullopt;
}

const TableCellSource* findTableCell(const TableSource& table, size_t node) {
  for (const auto& cell : table.header.cells) {
    if (cell.node == node) {
      return &cell;
    }
  }
  for (const auto& row : table.body) {
    for (const auto& cell : row.cells) {
      if (cell.node == node) {
        return &cell;
      }
    }
  }
  return nullptr;
}

EditResult insertTableCellTextSource(const Document& document,
                                     const InsertTableCellText& edit) {
  if (edit.cell >= document.nodes.size() ||
      document.nodes[edit.cell].kind != NodeKind::kBlock ||
      (document.nodes[edit.cell].type != MD_BLOCK_TH &&
       document.nodes[edit.cell].type != MD_BLOCK_TD)) {
    return {std::nullopt, QStringLiteral("Invalid table cell target")};
  }
  const auto tableNode = tableNodeForCell(document, edit.cell);
  if (!tableNode || document.nodes[*tableNode].parent !=
                        std::optional<size_t>(0)) {
    return {std::nullopt,
            QStringLiteral("Table cell text insertion supports only top-level "
                           "tables")};
  }
  const auto mapped = mapTable(document, *tableNode);
  if (!mapped.table) {
    return {std::nullopt, mapped.error};
  }
  const auto* cell = findTableCell(*mapped.table, edit.cell);
  if (!cell || cell->range.size != 0 ||
      !document.nodes[edit.cell].children.empty()) {
    return {std::nullopt, QStringLiteral("Table cell is not empty")};
  }
  QString payloadError;
  if (edit.text.isEmpty() || !validTableCellPayload(edit.text, payloadError)) {
    return {std::nullopt,
            QStringLiteral("Table cell text must be nonempty single-line UTF-8")};
  }
  const auto escaped = escapeTableCell(edit.text);
  const auto tableSource = document.source.mid(mapped.table->range.offset,
                                               mapped.table->range.size);
  const auto lineStart = lineStartAt(document.source, cell->range.offset);
  auto lineEnd = document.source.indexOf('\n', cell->range.offset);
  if (lineEnd < 0) {
    lineEnd = document.source.size();
  }
  auto bodyEnd = lineEnd;
  if (bodyEnd > lineStart && document.source[bodyEnd - 1] == '\r') {
    --bodyEnd;
  }
  qsizetype previousPipe = -1;
  for (auto i = cell->range.offset - 1; i >= lineStart; --i) {
    if (document.source[i] == '|' &&
        !escapedAt(document.source, lineStart, i)) {
      previousPipe = i;
      break;
    }
  }
  qsizetype nextPipe = -1;
  for (auto i = cell->range.offset + cell->range.size; i < bodyEnd; ++i) {
    if (document.source[i] == '|' &&
        !escapedAt(document.source, lineStart, i)) {
      nextPipe = i;
      break;
    }
  }
  if (previousPipe < 0 || nextPipe <= previousPipe) {
    return {std::nullopt,
            QStringLiteral("Table cell insertion requires outer pipes")};
  }
  const auto segmentOffset = previousPipe + 1;
  const auto segmentSize = nextPipe - segmentOffset;
  const auto replacement = QByteArray(" ") + escaped + " ";
  const auto relative = segmentOffset - mapped.table->range.offset;
  const auto relativeEnd =
      relative + segmentSize;
  if (relative < 0 || relativeEnd > tableSource.size()) {
    return {std::nullopt, QStringLiteral("Invalid table cell range")};
  }
  auto newTableSource = tableSource.left(relative);
  newTableSource += replacement;
  newTableSource += tableSource.mid(relativeEnd);
  const auto definitions = definitionInventory(document);
  QString contextError;
  auto parsed = parseTableContext(
      document, definitions, mapped.table->range.offset,
      mapped.table->range.offset + mapped.table->range.size, newTableSource,
      contextError);
  if (!parsed) {
    return {std::nullopt, contextError};
  }
  auto expected = graftList(document, *tableNode, *parsed);
  return apply(document, expected, {segmentOffset, segmentSize}, replacement);
}

std::optional<qsizetype> topLevelBlockStart(const Document& document,
                                            size_t node) {
  QString error;
  const auto range = mapTopLevelBlockRange(document, node, error);
  if (range) {
    return range->offset;
  }
  for (const auto& envelope : evidenceEnvelopes(document)) {
    if (envelope.node == node) {
      return lineStartAt(document.source, envelope.range.offset);
    }
  }
  return std::nullopt;
}

struct TopLevelInsertion {
  qsizetype offset;
  QByteArray prefix;
  QByteArray suffix;
};

std::optional<TopLevelInsertion> topLevelInsertion(const Document& document,
                                                   size_t position,
                                                   QString& error) {
  if (document.nodes.empty() ||
      document.nodes[0].kind != NodeKind::kBlock ||
      document.nodes[0].type != MD_BLOCK_DOC) {
    error = QStringLiteral("Invalid block insertion");
    return std::nullopt;
  }
  const auto& root = document.nodes[0];
  if (position > root.children.size()) {
    error = QStringLiteral("Invalid block insertion position");
    return std::nullopt;
  }
  qsizetype offset = document.source.size();
  if (position < root.children.size()) {
    const auto start = topLevelBlockStart(document, root.children[position]);
    if (!start) {
      error = QStringLiteral("Cannot find block insertion boundary");
      return std::nullopt;
    }
    offset = *start;
  }
  const auto eol = documentEol(document.source);
  QByteArray prefix;
  if (offset > 0) {
    const auto before = document.source.left(offset);
    const bool blank = before.endsWith("\n\n") ||
                       before.endsWith("\r\n\r\n") || before.endsWith("\r\r");
    const bool one = before.endsWith('\n') || before.endsWith('\r');
    if (!blank) {
      prefix = one ? eol : eol + eol;
    }
  }
  const QByteArray suffix = offset < document.source.size() ? eol : QByteArray{};
  return TopLevelInsertion{offset, prefix, suffix};
}

EditResult insertTopLevelFragment(const Document& document, size_t position,
                                  const QByteArray& fragment,
                                  int expectedType) {
  QString error;
  const auto insertion = topLevelInsertion(document, position, error);
  if (!insertion) {
    return {std::nullopt, error};
  }
  auto parsed = parse(fragment);
  if (!parsed.document || parsed.document->nodes[0].children.size() != 1) {
    return {std::nullopt, QStringLiteral("Structural block insertion failed")};
  }
  const auto child = parsed.document->nodes[0].children.front();
  if (parsed.document->nodes[child].type != expectedType) {
    return {std::nullopt, QStringLiteral("Structural block insertion failed")};
  }
  auto expected = document;
  const auto newIndex = copySubtree(expected, *parsed.document, child, 0);
  expected.nodes[0].children.insert(
      expected.nodes[0].children.begin() + qsizetype(position), newIndex);
  return apply(document, expected, {insertion->offset, 0},
               insertion->prefix + fragment + insertion->suffix);
}

EditResult insertTableSource(const Document& document, const InsertTable& edit) {
  if (edit.columns == 0 || edit.columns > 1000 || edit.bodyRows > 10000) {
    return {std::nullopt, QStringLiteral("Invalid table dimensions")};
  }
  const auto eol = documentEol(document.source);
  const auto row = [&](bool delimiter) {
    QByteArray value;
    for (size_t column = 0; column < edit.columns; ++column) {
      value += delimiter ? QByteArray("| --- ") : QByteArray("|  ");
    }
    value += '|';
    value += eol;
    return value;
  };
  QByteArray table;
  for (size_t column = 0; column < edit.columns; ++column) {
    table += "| Column " + QByteArray::number(qulonglong(column + 1)) + ' ';
  }
  table += '|';
  table += eol;
  table += row(true);
  for (size_t body = 0; body < edit.bodyRows; ++body) {
    table += row(false);
  }
  return insertTopLevelFragment(document, edit.position, table,
                                MD_BLOCK_TABLE);
}

EditResult insertHorizontalRuleSource(const Document& document,
                                      const InsertHorizontalRule& edit) {
  const auto fragment = QByteArray("---") + documentEol(document.source);
  return insertTopLevelFragment(document, edit.position, fragment,
                                MD_BLOCK_HR);
}

EditResult insertFencedCodeSource(const Document& document,
                                  const InsertFencedCode& edit) {
  const auto eol = documentEol(document.source);
  const auto fragment =
      QByteArray("```") + eol + QByteArray("```") + eol;
  return insertTopLevelFragment(document, edit.position, fragment,
                                MD_BLOCK_CODE);
}

EditResult outdentListItemSource(const Document& document,
                                 const OutdentListItem& edit) {
  QString error;
  auto mapped = mapItem(document, edit.item, error);
  if (!mapped) {
    return {std::nullopt, error};
  }
  const auto nestedList = mapped->list;
  const auto& nested = mapped->source;
  const auto enclosing = document.nodes[nestedList].parent;
  if (!enclosing ||
      document.nodes[*enclosing].type != MD_BLOCK_LI) {
    return {std::nullopt,
            QStringLiteral("Invalid target: list is top-level")};
  }
  const auto outerList = document.nodes[*enclosing].parent;
  if (!outerList || (document.nodes[*outerList].type != MD_BLOCK_UL &&
                     document.nodes[*outerList].type != MD_BLOCK_OL)) {
    return {std::nullopt, QStringLiteral("Invalid target: no outer list")};
  }
  const auto outer = mapList(document, *outerList);
  if (!outer.list) {
    return {std::nullopt, outer.error};
  }
  size_t enclosingIndex = outer.list->items.size();
  for (size_t i = 0; i < outer.list->items.size(); ++i) {
    if (outer.list->items[i].node == *enclosing) {
      enclosingIndex = i;
      break;
    }
  }
  if (enclosingIndex >= outer.list->items.size()) {
    return {std::nullopt, QStringLiteral("Invalid target: enclosing item")};
  }
  const auto movedContent = itemContent(document, nested.items[mapped->index]);
  auto enclosingContent =
      itemContent(document, outer.list->items[enclosingIndex]);
  const auto eol = listEol(document, *outer.list);
  const auto nestedRange =
      contentRangeFor(document, outer.list->items[enclosingIndex],
                      nested.range);

  auto nestedContents = std::vector<QByteArray>{};
  for (size_t i = 0; i < nested.items.size(); ++i) {
    if (i != mapped->index) {
      nestedContents.push_back(itemContent(document, nested.items[i]));
    }
  }
  auto nestedGaps = itemGaps(document, nested);
  removeGapForItem(nestedGaps, mapped->index, nested.items.size());
  const auto nestedMarker =
      listMarkerAt(document.source, nested.range.offset);
  if (!nestedMarker) {
    return {std::nullopt, QStringLiteral("Unsupported list marker")};
  }
  const auto nestedOrdered =
      document.nodes[nestedList].type == MD_BLOCK_OL;
  const auto nestedStart =
      nestedOrdered
          ? document.nodes[nestedList].attributes["start"].toInt()
          : 0;
  QByteArray nestedSource;
  if (!nestedContents.empty()) {
    const auto nestedReal = buildListSource(
        nestedContents, nestedGaps, nestedOrdered, nestedStart,
        nestedMarker->delimiter, eol,
        !listEndsAtEofWithoutEol(document, nested),
        nested.continuationPrefix, nested.blankLinePrefix);
    nestedSource = stripContainerPrefix(
        nestedReal, nested.continuationPrefix, nested.blankLinePrefix);
  }
  if (!nestedRange) {
    return {std::nullopt, QStringLiteral("Unsupported nested list range")};
  }
  enclosingContent =
      replaceContentRange(enclosingContent, *nestedRange, nestedSource);

  auto contents = std::vector<QByteArray>{};
  for (size_t i = 0; i < outer.list->items.size(); ++i) {
    contents.push_back(i == enclosingIndex
                           ? enclosingContent
                           : itemContent(document, outer.list->items[i]));
  }
  auto gaps = itemGaps(document, *outer.list);
  const auto outerMarker =
      listMarkerAt(document.source, outer.list->range.offset);
  if (!outerMarker) {
    return {std::nullopt, QStringLiteral("Unsupported list marker")};
  }
  const auto outerOrdered =
      document.nodes[*outerList].type == MD_BLOCK_OL;
  const auto outerStart =
      outerOrdered ? document.nodes[*outerList].attributes["start"].toInt() : 0;
  if (outerOrdered &&
      qint64(outerStart) + qint64(contents.size()) > 999999999) {
    return {std::nullopt, QStringLiteral("Ordered list number overflow")};
  }
  const auto outerGap = gapFor(
      !document.nodes[*outerList].attributes["tight"].toBool(),
      outer.list->continuationPrefix, outer.list->blankLinePrefix, eol);
  contents.insert(contents.begin() + qsizetype(enclosingIndex + 1),
                  movedContent);
  if (gaps.empty()) {
    gaps.push_back(outerGap);
  } else if (enclosingIndex >= gaps.size()) {
    gaps.push_back(outerGap);
  } else {
    gaps.insert(gaps.begin() + qsizetype(enclosingIndex), outerGap);
  }
  const auto trailingEol = !listEndsAtEofWithoutEol(document, *outer.list);
  const auto newSource = buildListSource(
      contents, gaps, outerOrdered, outerStart, outerMarker->delimiter, eol,
      trailingEol, outer.list->continuationPrefix,
      outer.list->blankLinePrefix);
  const auto canonicalSource = stripContainerPrefix(
      newSource, outer.list->continuationPrefix, outer.list->blankLinePrefix);
  const auto definitions = definitionInventory(document);
  QString contextError;
  auto parsed = parseListContext(document, definitions, outer.list->range.offset,
                                 outer.list->range.offset + outer.list->range.size,
                                 canonicalSource, contextError);
  if (!parsed) {
    return {std::nullopt, contextError};
  }
  const auto& parsedList = parsed->document.nodes[parsed->root];
  if (parsedList.children.size() != outer.list->items.size() + 1) {
    return {std::nullopt, QStringLiteral("Structural list mismatch")};
  }

  auto expected = graftList(document, *outerList, *parsed);
  return apply(document, expected, outer.list->range, newSource);
}


EditResult wrapInLinkSource(const Document& document,
                            const WrapInLink& edit) {
  if (edit.node >= document.nodes.size()) {
    return {std::nullopt, QStringLiteral("Invalid link text target")};
  }
  const auto& node = document.nodes[edit.node];
  const auto& range = edit.textRange;
  if (node.kind != NodeKind::kText || node.type != MD_TEXT_NORMAL ||
      !node.source || node.source->offset < 0 ||
      node.source->offset > document.source.size() ||
      node.source->size != node.text.size() ||
      node.source->size > document.source.size() - node.source->offset ||
      document.source.mid(node.source->offset, node.source->size) !=
          node.text ||
      range.offset < 0 || range.size <= 0 ||
      range.offset > node.text.size() ||
      range.size > node.text.size() - range.offset) {
    return {std::nullopt,
            QStringLiteral("Link creation requires a mapped text selection")};
  }
  const auto selected = node.text.mid(range.offset, range.size);
  if (selected.isEmpty() || !validUtf8(selected) ||
      selected.contains('\0') || selected.contains('\r') ||
      selected.contains('\n')) {
    return {std::nullopt, QStringLiteral("Invalid link label selection")};
  }
  if (edit.url.isEmpty() || !validUtf8(edit.url) ||
      edit.url.contains('\0') || edit.url.contains('\r') ||
      edit.url.contains('\n') || edit.url.contains('<') ||
      edit.url.contains('>') || edit.url.contains(' ') ||
      edit.url.contains('\t')) {
    return {std::nullopt, QStringLiteral("Invalid link URL")};
  }
  auto ancestor = node.parent;
  while (ancestor) {
    if (*ancestor >= document.nodes.size()) {
      return {std::nullopt, QStringLiteral("Invalid link ancestry")};
    }
    const auto& parent = document.nodes[*ancestor];
    if (parent.kind == NodeKind::kSpan &&
        (parent.type == MD_SPAN_A || parent.type == MD_SPAN_IMG)) {
      return {std::nullopt,
              QStringLiteral("Nested links/images are not supported")};
    }
    ancestor = parent.parent;
  }
  const auto replacement = QByteArray("[") + selected + QByteArray("](<") +
                           edit.url + QByteArray(">)");
  auto parsed = parse(replacement);
  if (!parsed.document || parsed.document->nodes[0].children.size() != 1) {
    return {std::nullopt, QStringLiteral("Invalid link source")};
  }
  const auto paragraph = parsed.document->nodes[0].children.front();
  if (parsed.document->nodes[paragraph].type != MD_BLOCK_P ||
      parsed.document->nodes[paragraph].children.size() != 1) {
    return {std::nullopt, QStringLiteral("Invalid link source")};
  }
  const auto link = parsed.document->nodes[paragraph].children.front();
  if (parsed.document->nodes[link].type != MD_SPAN_A) {
    return {std::nullopt, QStringLiteral("Invalid link source")};
  }
  const auto parent = node.parent;
  if (!parent || *parent >= document.nodes.size()) {
    return {std::nullopt, QStringLiteral("Invalid link parent")};
  }
  auto& siblings = document.nodes[*parent].children;
  const auto position = std::find(siblings.begin(), siblings.end(), edit.node);
  if (position == siblings.end()) {
    return {std::nullopt, QStringLiteral("Invalid link parent")};
  }
  auto expected = document;
  std::vector<size_t> replacementNodes;
  const auto addText = [&](const QByteArray& text) {
    auto copy = document.nodes[edit.node];
    copy.text = text;
    copy.children.clear();
    copy.source.reset();
    expected.nodes.push_back(std::move(copy));
    replacementNodes.push_back(expected.nodes.size() - 1);
  };
  const auto before = node.text.left(range.offset);
  const auto after = node.text.mid(range.offset + range.size);
  if (!before.isEmpty()) {
    addText(before);
  }
  const auto linkIndex = copySubtree(expected, *parsed.document, link, *parent);
  replacementNodes.push_back(linkIndex);
  if (!after.isEmpty()) {
    addText(after);
  }
  const auto index = size_t(position - siblings.begin());
  auto& expectedSiblings = expected.nodes[*parent].children;
  expectedSiblings.erase(expectedSiblings.begin() + qsizetype(index));
  expectedSiblings.insert(expectedSiblings.begin() + qsizetype(index),
                          replacementNodes.begin(), replacementNodes.end());
  return apply(document, expected,
               {node.source->offset + range.offset, range.size}, replacement);
}

EditResult insertImageSource(const Document& document,
                             const InsertImage& edit) {
  if (edit.node >= document.nodes.size()) {
    return {std::nullopt, QStringLiteral("Invalid image text target")};
  }
  const auto& node = document.nodes[edit.node];
  const auto& range = edit.textRange;
  if (node.kind != NodeKind::kText || node.type != MD_TEXT_NORMAL ||
      !node.source || node.source->offset < 0 ||
      node.source->offset > document.source.size() ||
      node.source->size != node.text.size() ||
      node.source->size > document.source.size() - node.source->offset ||
      document.source.mid(node.source->offset, node.source->size) !=
          node.text ||
      range.offset < 0 || range.size <= 0 ||
      range.offset > node.text.size() ||
      range.size > node.text.size() - range.offset) {
    return {std::nullopt,
            QStringLiteral("Image creation requires a mapped text selection")};
  }
  const auto alt = node.text.mid(range.offset, range.size);
  if (alt.isEmpty() || !validUtf8(alt) || alt.contains('\0') ||
      alt.contains('\r') || alt.contains('\n')) {
    return {std::nullopt, QStringLiteral("Invalid image alt selection")};
  }
  if (edit.src.isEmpty() || !validUtf8(edit.src) ||
      edit.src.contains('\0') || edit.src.contains('\r') ||
      edit.src.contains('\n') || edit.src.contains('<') ||
      edit.src.contains('>') || edit.src.contains(' ') ||
      edit.src.contains('\t')) {
    return {std::nullopt, QStringLiteral("Invalid image source")};
  }
  auto ancestor = node.parent;
  while (ancestor) {
    if (*ancestor >= document.nodes.size()) {
      return {std::nullopt, QStringLiteral("Invalid image ancestry")};
    }
    const auto& parent = document.nodes[*ancestor];
    if (parent.kind == NodeKind::kSpan &&
        (parent.type == MD_SPAN_A || parent.type == MD_SPAN_IMG)) {
      return {std::nullopt,
              QStringLiteral("Nested links/images are not supported")};
    }
    ancestor = parent.parent;
  }
  const auto replacement = QByteArray("![") + alt + QByteArray("](<") +
                           edit.src + QByteArray(">)");
  auto parsed = parse(replacement);
  if (!parsed.document || parsed.document->nodes[0].children.size() != 1) {
    return {std::nullopt, QStringLiteral("Invalid image source")};
  }
  const auto paragraph = parsed.document->nodes[0].children.front();
  if (parsed.document->nodes[paragraph].type != MD_BLOCK_P ||
      parsed.document->nodes[paragraph].children.size() != 1) {
    return {std::nullopt, QStringLiteral("Invalid image source")};
  }
  const auto image = parsed.document->nodes[paragraph].children.front();
  if (parsed.document->nodes[image].type != MD_SPAN_IMG) {
    return {std::nullopt, QStringLiteral("Invalid image source")};
  }
  const auto parent = node.parent;
  if (!parent || *parent >= document.nodes.size()) {
    return {std::nullopt, QStringLiteral("Invalid image parent")};
  }
  auto& siblings = document.nodes[*parent].children;
  const auto position = std::find(siblings.begin(), siblings.end(), edit.node);
  if (position == siblings.end()) {
    return {std::nullopt, QStringLiteral("Invalid image parent")};
  }
  auto expected = document;
  std::vector<size_t> replacementNodes;
  const auto addText = [&](const QByteArray& text) {
    auto copy = document.nodes[edit.node];
    copy.text = text;
    copy.children.clear();
    copy.source.reset();
    expected.nodes.push_back(std::move(copy));
    replacementNodes.push_back(expected.nodes.size() - 1);
  };
  const auto before = node.text.left(range.offset);
  const auto after = node.text.mid(range.offset + range.size);
  if (!before.isEmpty()) {
    addText(before);
  }
  const auto imageIndex = copySubtree(expected, *parsed.document, image,
                                      *parent);
  replacementNodes.push_back(imageIndex);
  if (!after.isEmpty()) {
    addText(after);
  }
  const auto index = size_t(position - siblings.begin());
  auto& expectedSiblings = expected.nodes[*parent].children;
  expectedSiblings.erase(expectedSiblings.begin() + qsizetype(index));
  expectedSiblings.insert(expectedSiblings.begin() + qsizetype(index),
                          replacementNodes.begin(), replacementNodes.end());
  return apply(document, expected,
               {node.source->offset + range.offset, range.size}, replacement);
}

EditResult toggleInlineSource(const Document& document,
                              const ToggleInlineStyle& edit) {
  const int type = spanType(edit.style);
  if (type < 0 || edit.node >= document.nodes.size()) {
    return {std::nullopt, QStringLiteral("Invalid inline style or node")};
  }
  const auto& node = document.nodes[edit.node];
  const auto range = edit.textRange;
  if (node.kind != NodeKind::kText || node.type != MD_TEXT_NORMAL ||
      !node.source || !node.parent || node.source->offset < 0 ||
      node.source->offset > document.source.size() ||
      node.source->size != node.text.size() ||
      node.source->size > document.source.size() - node.source->offset ||
      document.source.mid(node.source->offset, node.source->size) !=
          node.text ||
      range.offset < 0 || range.size <= 0 || range.offset > node.text.size() ||
      range.size > node.text.size() - range.offset) {
    return {std::nullopt,
            QStringLiteral("Inline style requires a mapped text selection")};
  }
  const auto before = node.text.left(range.offset);
  const auto selected = node.text.mid(range.offset, range.size);
  const auto after = node.text.mid(range.offset + range.size);
  if (!validUtf8(before) || !validUtf8(selected) || !validUtf8(after) ||
      node.text.contains('\0') || node.text.contains('\r') ||
      node.text.contains('\n')) {
    return {std::nullopt,
            QStringLiteral("Invalid single-line UTF-8 selection")};
  }
  const auto text = QString::fromUtf8(selected);
  if (text.isEmpty() || text.front().isSpace() || text.back().isSpace()) {
    return {std::nullopt,
            QStringLiteral("Selection must not start or end with whitespace")};
  }
  bool applied = false;
  auto ancestor = node.parent;
  while (ancestor) {
    if (*ancestor >= document.nodes.size()) {
      return {std::nullopt, QStringLiteral("Invalid inline ancestry")};
    }
    const auto& parent = document.nodes[*ancestor];
    if (parent.kind == NodeKind::kSpan) {
      if (parent.type != MD_SPAN_EM && parent.type != MD_SPAN_STRONG &&
          parent.type != MD_SPAN_DEL) {
        return {std::nullopt,
                QStringLiteral("This inline fragment is protected")};
      }
      applied = applied || parent.type == type;
    } else if (parent.kind != NodeKind::kBlock ||
               (parent.type != MD_BLOCK_DOC && parent.type != MD_BLOCK_P &&
                parent.type != MD_BLOCK_H && parent.type != MD_BLOCK_LI &&
                parent.type != MD_BLOCK_UL && parent.type != MD_BLOCK_OL &&
                parent.type != MD_BLOCK_QUOTE)) {
      return {std::nullopt, QStringLiteral("This block is protected")};
    }
    ancestor = parent.parent;
  }
  auto expected = document;
  if (applied) {
    const auto spanIndex = *node.parent;
    const auto& span = document.nodes[spanIndex];
    if (span.kind != NodeKind::kSpan || span.type != type ||
        span.children.size() != 1 || span.children.front() != edit.node ||
        !span.parent || range.offset != 0 || range.size != node.text.size()) {
      return {std::nullopt,
              QStringLiteral("Remove style only from an entire simple span; "
                             "use Markdown for partial removal")};
    }
    const int width = edit.style == InlineStyle::kItalic ? 1 : 2;
    const auto first = node.source->offset;
    const auto end = first + node.source->size;
    if (first < width || width > document.source.size() - end) {
      return {std::nullopt,
              QStringLiteral("Inline delimiters have no proven range")};
    }
    const auto opening = document.source.mid(first - width, width);
    const auto closing = document.source.mid(end, width);
    const bool allowed = edit.style == InlineStyle::kStrikethrough
                             ? opening == "~~"
                             : opening == QByteArray(width, '*') ||
                                   opening == QByteArray(width, '_');
    if (!allowed || opening != closing) {
      return {std::nullopt,
              QStringLiteral("Inline delimiters have no proven range")};
    }
    auto& siblings = expected.nodes[*span.parent].children;
    const auto position =
        std::find(siblings.begin(), siblings.end(), spanIndex);
    if (position == siblings.end()) {
      return {std::nullopt, QStringLiteral("Invalid inline parent")};
    }
    *position = edit.node;
    expected.nodes[edit.node].parent = span.parent;
    return apply(document, expected,
                 {first - width, node.source->size + 2 * width}, node.text);
  }

  const QByteArray delimiter = edit.style == InlineStyle::kBold     ? "**"
                               : edit.style == InlineStyle::kItalic ? "*"
                                                                    : "~~";
  const auto parent = *node.parent;
  const auto spanIndex = expected.nodes.size();
  const auto childIndex = spanIndex + 1;
  expected.nodes.push_back(
      {NodeKind::kSpan, type, {}, {}, {childIndex}, std::nullopt, parent});
  expected.nodes.push_back({NodeKind::kText,
                            MD_TEXT_NORMAL,
                            {},
                            selected,
                            {},
                            std::nullopt,
                            spanIndex});
  std::vector<size_t> replacement;
  if (!before.isEmpty()) {
    replacement.push_back(expected.nodes.size());
    expected.nodes.push_back({NodeKind::kText,
                              MD_TEXT_NORMAL,
                              {},
                              before,
                              {},
                              std::nullopt,
                              parent});
  }
  replacement.push_back(spanIndex);
  if (!after.isEmpty()) {
    replacement.push_back(expected.nodes.size());
    expected.nodes.push_back(
        {NodeKind::kText, MD_TEXT_NORMAL, {}, after, {}, std::nullopt, parent});
  }
  auto& siblings = expected.nodes[parent].children;
  const auto position = std::find(siblings.begin(), siblings.end(), edit.node);
  if (position == siblings.end()) {
    return {std::nullopt, QStringLiteral("Invalid text parent")};
  }
  const auto offset = position - siblings.begin();
  siblings.erase(position);
  siblings.insert(siblings.begin() + offset, replacement.begin(),
                  replacement.end());
  return apply(document, expected,
               {node.source->offset + range.offset, range.size},
               delimiter + selected + delimiter);
}

}  // namespace

std::optional<FencedCodeBodySource> mapFencedCodeBody(
    const Document& document, size_t node, QString& error) {
  return mapFencedCodeBodyInternal(document, node, error);
}

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
        } else if constexpr (std::is_same_v<Command, SetHeadingLevel>) {
          return setHeadingLevelSource(document, edit.node, edit.level);
        } else if constexpr (std::is_same_v<Command, InsertListItem>) {
          return insertListItemSource(document, edit);
        } else if constexpr (std::is_same_v<Command, RemoveListItem>) {
          return removeListItemSource(document, edit);
        } else if constexpr (std::is_same_v<Command, SetListKind>) {
          return setListKindSource(document, edit);
        } else if constexpr (std::is_same_v<Command, SetItemTask>) {
          return setItemTaskSource(document, edit);
        } else if constexpr (std::is_same_v<Command, WrapInQuote>) {
          return wrapInQuoteSource(document, edit);
        } else if constexpr (std::is_same_v<Command, UnwrapQuote>) {
          return unwrapQuoteSource(document, edit);
        } else if constexpr (std::is_same_v<Command, WrapInList>) {
          return wrapInListSource(document, edit);
        } else if constexpr (std::is_same_v<Command, ReplaceCodeBody>) {
          return replaceCodeBodySource(document, edit);
        } else if constexpr (std::is_same_v<Command, IndentListItem>) {
          return indentListItemSource(document, edit);
        } else if constexpr (std::is_same_v<Command, OutdentListItem>) {
          return outdentListItemSource(document, edit);
        } else if constexpr (std::is_same_v<Command, InsertTableRow>) {
          return insertTableRowSource(document, edit);
        } else if constexpr (std::is_same_v<Command, RemoveTableRow>) {
          return removeTableRowSource(document, edit);
        } else if constexpr (std::is_same_v<Command, SetTableAlignment>) {
          return setTableAlignmentSource(document, edit);
        } else if constexpr (std::is_same_v<Command, InsertTableColumn>) {
          return insertTableColumnSource(document, edit);
        } else if constexpr (std::is_same_v<Command, RemoveTableColumn>) {
          return removeTableColumnSource(document, edit);
        } else if constexpr (std::is_same_v<Command, RemoveTable>) {
          return removeTableSource(document, edit);
        } else if constexpr (std::is_same_v<Command, InsertTableCellText>) {
          return insertTableCellTextSource(document, edit);
        } else if constexpr (std::is_same_v<Command, InsertTable>) {
          return insertTableSource(document, edit);
        } else if constexpr (std::is_same_v<Command, InsertHorizontalRule>) {
          return insertHorizontalRuleSource(document, edit);
        } else if constexpr (std::is_same_v<Command, InsertFencedCode>) {
          return insertFencedCodeSource(document, edit);
        } else if constexpr (std::is_same_v<Command, WrapInLink>) {
          return wrapInLinkSource(document, edit);
        } else if constexpr (std::is_same_v<Command, InsertImage>) {
          return insertImageSource(document, edit);
        } else if constexpr (std::is_same_v<Command, ReplaceInlineCode>) {
          return replaceInlineCodeSource(document, edit);
        } else {
          return toggleInlineSource(document, edit);
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

}  // namespace markpad::markdown
