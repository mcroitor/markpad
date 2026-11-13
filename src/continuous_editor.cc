#include "continuous_editor.h"

#include <QApplication>
#include <QColor>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QPalette>
#include <QSet>
#include <QSignalBlocker>
#include <QStringConverter>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextTable>
#include <QTextTableCell>
#include <QTextTableCellFormat>
#include <QTextTableFormat>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>

#include "page_text_edit.h"
#include "page_canvas.h"

namespace markpad {
namespace {

class PlainTextOnlyEdit final : public PageTextEdit {
 public:
  explicit PlainTextOnlyEdit(QWidget* parent) : PageTextEdit(parent) {
    setAcceptRichText(false);
    setAcceptDrops(false);
    setUndoRedoEnabled(false);
  }

 protected:
  void insertFromMimeData(const QMimeData* data) override {
    if (data->hasText()) {
      insertPlainText(data->text());
    }
  }
  QVariant loadResource(int, const QUrl&) override { return {}; }
};

QString rawText(QTextEdit* editor) {
  auto text = editor->document()->toRawText();
  text.replace(QChar::ParagraphSeparator, QChar('\n'));
  return text;
}

QString literal(const QByteArray& bytes) {
  QStringDecoder decoder(QStringConverter::Utf8,
                         QStringConverter::Flag::ConvertInitialBom);
  return decoder(bytes);
}

bool darkPalette(const QPalette& palette) {
  return palette.color(QPalette::Window).lightness() < 128;
}

QColor mutedForeground(const QPalette& palette) {
  return darkPalette(palette) ? QColor(175, 175, 175) : QColor(100, 100, 100);
}

QColor mutedBackground(const QPalette& palette) {
  return darkPalette(palette) ? QColor(62, 62, 62) : QColor(235, 235, 235);
}

QColor codeForeground(const QPalette& palette) {
  return darkPalette(palette) ? QColor(225, 225, 225) : QColor(70, 70, 70);
}

QColor codeBackground(const QPalette& palette) {
  return darkPalette(palette) ? QColor(55, 55, 55) : QColor(235, 235, 235);
}

QColor linkForeground(const QPalette& palette) {
  return darkPalette(palette) ? QColor(120, 170, 255) : QColor(35, 80, 170);
}

QColor imageForeground(const QPalette& palette) {
  return darkPalette(palette) ? QColor(150, 200, 240) : QColor(40, 90, 140);
}

QColor imageBackground(const QPalette& palette) {
  return darkPalette(palette) ? QColor(40, 60, 80) : QColor(225, 235, 245);
}

QColor ruleForeground(const QPalette& palette) {
  return darkPalette(palette) ? QColor(160, 160, 160) : QColor(120, 120, 120);
}

int taskMarkerPosition(const QString& prefix) {
  const auto unchecked = prefix.indexOf(QStringLiteral("[ ] "));
  const auto checked = prefix.indexOf(QStringLiteral("[x] "));
  if (unchecked >= 0) {
    return int(unchecked);
  }
  return checked >= 0 ? int(checked) : -1;
}

QFont configuredCodeFont(const WysiwygStyle& style) {
  QFont font = style.codeFontFamily.trimmed().isEmpty()
                   ? QFontDatabase::systemFont(QFontDatabase::FixedFont)
                   : QFont(style.codeFontFamily);
  font.setStyleHint(QFont::Monospace);
  font.setPointSize(std::clamp(style.codeFontSize, 6, 72));
  return font;
}

void applyHeadingStyle(QTextCharFormat& format, const WysiwygStyle& style,
                       int level) {
  if (!style.headingFontFamily.trimmed().isEmpty()) {
    format.setFontFamilies(QStringList{style.headingFontFamily});
  }
  format.setFontWeight(style.headingBold ? QFont::Bold : QFont::Normal);
  format.setFontPointSize(std::clamp(
      style.headingSizes[size_t(std::clamp(level, 1, 6) - 1)], 6, 96));
}

void applyCodeBlockBackground(QTextCursor cursor, int start, int end,
                              const QColor& color) {
  auto* document = cursor.document();
  for (auto block = document->findBlock(start);
       block.isValid() && block.position() < end; block = block.next()) {
    auto format = block.blockFormat();
    format.setBackground(color);
    format.setProperty(kPageUnitProperty, start + 1);
    format.setTopMargin(0);
    format.setBottomMargin(block.next().isValid() &&
                                   block.next().position() < end
                               ? 0
                               : 8);
    QTextCursor blockCursor(block);
    blockCursor.setBlockFormat(format);
  }
}

std::optional<QString> codeDisplay(const QByteArray& bytes) {
  QStringDecoder decoder(QStringConverter::Utf8);
  QString text = decoder(bytes);
  if (decoder.hasError()) {
    return std::nullopt;
  }
  text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  text.replace(QChar('\r'), QChar('\n'));
  if (text.endsWith(QChar('\n'))) {
    text.chop(1);
  }
  return text;
}

Qt::Alignment tableAlignment(markdown::TableAlignment alignment) {
  switch (alignment) {
    case markdown::TableAlignment::kLeft:
      return Qt::AlignLeft;
    case markdown::TableAlignment::kCenter:
      return Qt::AlignHCenter;
    case markdown::TableAlignment::kRight:
      return Qt::AlignRight;
    case markdown::TableAlignment::kNone:
    default:
      return Qt::AlignLeft;
  }
}

bool editable(const markdown::Document& document, const markdown::Node& node) {
  return node.kind == markdown::NodeKind::kText &&
         node.type == MD_TEXT_NORMAL && node.source &&
         node.source->size == node.text.size() &&
         document.source.mid(node.source->offset, node.source->size) ==
             node.text &&
         !node.text.isEmpty() && !node.text.contains('\r') &&
         !node.text.contains('\n') &&
         !literal(node.text).contains(QChar::ParagraphSeparator) &&
         !literal(node.text).contains(QChar::LineSeparator);
}

}  // namespace

ContinuousEditor::ContinuousEditor(QWidget* parent)
    : QWidget(parent),
      editor_(new PlainTextOnlyEdit(this)),
      canvas_(new PageCanvas(this)) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  editor_->setObjectName(QStringLiteral("continuousEditor"));
  editor_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  canvas_->setObjectName(QStringLiteral("pagedCanvas"));
  canvas_->hide();
  layout->addWidget(editor_);
  layout->addWidget(canvas_);
  editor_->installEventFilter(this);
  connect(editor_->document(), &QTextDocument::contentsChange, this,
          [this](int position, int removed, int added) {
            if (!rendering_ && (removed || added)) {
              change_ = Change{position, removed, added};
            }
          });
  connect(editor_, &QTextEdit::textChanged, this, &ContinuousEditor::changed);
  connect(editor_, &QTextEdit::cursorPositionChanged, this,
          &ContinuousEditor::syncCursorToCanvas);
  connect(editor_, &QTextEdit::selectionChanged, this,
          &ContinuousEditor::syncCursorToCanvas);
  connect(canvas_, &PageCanvas::cursorPositionChanged, this,
          &ContinuousEditor::syncCursorToEditor);
}

QTextDocument* ContinuousEditor::ensureProjection(
    const DocumentSnapshot& snapshot, QString* error) {
  if (!document_ || !(token_ == snapshot.token)) {
    const auto message = render(snapshot);
    if (!message.isEmpty()) {
      if (error) {
        *error = message;
      }
      return nullptr;
    }
  }
  return editor_->document();
}

QString ContinuousEditor::render(const DocumentSnapshot& snapshot) {  const QSignalBlocker blocker(editor_);
  rendering_ = true;
  if (paged_ && canvas_) {
    canvas_->setLayoutSuspended(true);
  }
  token_ = snapshot.token;
  auto parsed = markdown::parse(QByteArray::fromStdString(snapshot.source),
                                snapshot.token.revision);
  document_ = std::move(parsed.document);
  runs_.clear();
  codeRuns_.clear();
  codeSpanRuns_.clear();
  tableRuns_.clear();
  emptyCellRuns_.clear();
  breakRuns_.clear();
  markerRuns_.clear();
  contexts_.clear();
  mappedBlocks_.clear();
  editor_->clear();
  firstBlock_ = true;
  editor_->setReadOnly(!document_.has_value());
  if (document_) {
    mappedBlocks_.resize(document_->nodes.size(), false);
    for (const auto& block : markdown::mapBlocks(*document_)) {
      mappedBlocks_[block.node] = true;
    }
    QTextCursor cursor(editor_->document());
    renderBlock(0, cursor);
    if (firstBlock_) {
      opaque(cursor, QStringLiteral("No rendered blocks - start in Markdown"),
             0);
    }
  } else {
    editor_->setPlainText(QStringLiteral("Cannot render: ") + parsed.error);
  }
  renderedText_ = rawText(editor_);
  change_.reset();
  QSet<int> breakPositions;
  for (const auto& run : breakRuns_) {
    breakPositions.insert(run.start);
  }
  editor_->setBreakPositions(breakPositions);
  if (canvas_) {
    canvas_->setBreakPositions(breakPositions);
  }
  if (paged_ && canvas_) {
    canvas_->setLayoutSuspended(false);
    canvas_->refreshLayout();
    canvas_->setTextCursor(editor_->textCursor());
  }
  rendering_ = false;
  return parsed.error;
}

void ContinuousEditor::beginBlock(QTextCursor& cursor, int depth,
                                  bool justify) {
  if (!firstBlock_) {
    cursor.insertBlock();
  }
  firstBlock_ = false;
  QTextBlockFormat block;
  block.setLeftMargin(depth * 20);
  block.setBottomMargin(8);
  block.setAlignment(justify ? Qt::AlignJustify : Qt::AlignLeft);
  cursor.setBlockFormat(block);
  cursor.setCharFormat(QTextCharFormat{});
}

void ContinuousEditor::opaque(QTextCursor& cursor, const QString& label,
                              int depth, std::optional<size_t> block,
                              std::optional<size_t> list,
                              std::optional<size_t> quote) {
  beginBlock(cursor, depth);
  const int start = cursor.position();
  const auto palette = QApplication::palette();
  QTextCharFormat format;
  format.setForeground(mutedForeground(palette));
  format.setBackground(mutedBackground(palette));
  cursor.insertText(QStringLiteral("[Read-only: ") + label + ']', format);
  if (block) {
    contexts_.push_back(
        {start, cursor.position(), {}, {}, *block, list, quote});
  }
}

void ContinuousEditor::renderCodeBlock(size_t index, QTextCursor& cursor,
                                       int depth, std::optional<size_t> list,
                                       std::optional<size_t> quote) {
  QString error;
  const auto mapped = markdown::mapFencedCodeBody(*document_, index, error);
  const auto display =
      mapped ? codeDisplay(document_->source.mid(mapped->body.offset,
                                                 mapped->body.size))
             : std::nullopt;
  if (!mapped || !display) {
    opaque(cursor, QStringLiteral("code - edit in Markdown"), depth, index,
           list, quote);
    return;
  }
  beginBlock(cursor, depth);
  const int start = cursor.position();
  const auto palette = QApplication::palette();
  QTextCharFormat format;
  format.setFont(configuredCodeFont(style_));
  format.setForeground(codeForeground(palette));
  format.setBackground(codeBackground(palette));
  format.setToolTip(QStringLiteral(
      "Editable fenced code body; fences and info string are protected."));
  cursor.insertText(*display, format);
  applyCodeBlockBackground(cursor, start, cursor.position(),
                           codeBackground(palette));
  codeRuns_.push_back({index, start, int(display->size())});
  contexts_.push_back(
      {start, cursor.position(), {}, {}, index, list, quote});
}

void ContinuousEditor::renderTable(size_t index, QTextCursor& cursor, int depth) {
  if (document_->nodes[index].parent != std::optional<size_t>(0)) {
    opaque(cursor, QStringLiteral("table - edit in Markdown"), depth);
    return;
  }
  const auto mapped = markdown::mapTable(*document_, index);
  if (!mapped.table) {
    opaque(cursor, QStringLiteral("table - edit in Markdown"), depth);
    return;
  }
  const auto& source = *mapped.table;
  const auto rows = source.body.size() + 1;
  if (source.columns == 0 || source.columns > 1000 || rows > 10000 ||
      source.alignments.size() != source.columns ||
      source.header.cells.size() != source.columns) {
    opaque(cursor, QStringLiteral("table - edit in Markdown"), depth);
    return;
  }
  for (const auto& row : source.body) {
    if (row.cells.size() != source.columns) {
      opaque(cursor, QStringLiteral("table - edit in Markdown"), depth);
      return;
    }
  }
  const auto palette = QApplication::palette();
  QTextTableFormat tableFormat;
  tableFormat.setBorder(1);
  tableFormat.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
  tableFormat.setBorderBrush(palette.color(QPalette::Mid));
  tableFormat.setCellSpacing(0);
  tableFormat.setCellPadding(4);
  tableFormat.setWidth(QTextLength(QTextLength::PercentageLength, 100));
  QList<QTextLength> columnConstraints;
  const auto columnWidth = 100.0 / double(source.columns);
  for (size_t column = 0; column < source.columns; ++column) {
    columnConstraints.append(
        QTextLength(QTextLength::PercentageLength, columnWidth));
  }
  tableFormat.setColumnWidthConstraints(columnConstraints);
  auto* table =
      cursor.insertTable(int(rows), int(source.columns), tableFormat);
  const auto fillRow = [&](const markdown::TableRowSource& row, int rowIndex,
                           bool header) {
    for (size_t column = 0; column < row.cells.size(); ++column) {
      auto cell = table->cellAt(rowIndex, int(column));
      if (header) {
        QTextTableCellFormat cellFormat;
        cellFormat.setBackground(mutedBackground(palette));
        cell.setFormat(cellFormat);
      }
      auto cellCursor = cell.firstCursorPosition();
      QTextBlockFormat blockFormat;
      blockFormat.setAlignment(
          tableAlignment(source.alignments[column]));
      cellCursor.setBlockFormat(blockFormat);
      QTextCharFormat format;
      if (header) {
        format.setFontWeight(QFont::Bold);
      }
      const auto cellNode = row.cells[column].node;
      if (cellNode >= document_->nodes.size()) {
        continue;
      }
      const auto cellStart = cellCursor.position();
      for (auto child : document_->nodes[cellNode].children) {
        renderInline(child, cellCursor, format);
      }
      if (document_->nodes[cellNode].children.empty()) {
        emptyCellRuns_.push_back({cellNode, cellStart});
      }
    }
  };
  fillRow(source.header, 0, true);
  for (size_t row = 0; row < source.body.size(); ++row) {
    fillRow(source.body[row], int(row + 1), false);
  }
  tableRuns_.push_back({table, index});
  firstBlock_ = false;
  cursor = editor_->document()->rootFrame()->lastCursorPosition();
}

void ContinuousEditor::renderHorizontalRule(QTextCursor& cursor, int depth) {
  beginBlock(cursor, depth);
  QTextBlockFormat blockFormat;
  blockFormat.setAlignment(Qt::AlignHCenter);
  cursor.setBlockFormat(blockFormat);
  QTextCharFormat format;
  format.setForeground(ruleForeground(QApplication::palette()));
  format.setToolTip(QStringLiteral("Horizontal rule - edit in Markdown"));
  cursor.insertText(QString(40, QChar(0x2500)), format);
}

void ContinuousEditor::renderBlock(size_t index, QTextCursor& cursor, int depth,
                                   QString prefix, std::optional<size_t> task,
                                   std::optional<size_t> list,
                                   std::optional<size_t> quote,
                                   std::optional<size_t> item) {
  const auto& node = document_->nodes[index];
  if (node.kind != markdown::NodeKind::kBlock) {
    return;
  }
  if (node.type == MD_BLOCK_DOC || node.type == MD_BLOCK_QUOTE ||
      node.type == MD_BLOCK_UL || node.type == MD_BLOCK_OL) {
    qint64 number = node.attributes["start"].toInteger(1);
    const auto childList =
        node.type == MD_BLOCK_UL || node.type == MD_BLOCK_OL
            ? std::optional<size_t>(index)
            : list;
    const auto childQuote =
        node.type == MD_BLOCK_QUOTE ? std::optional<size_t>(index) : quote;
    const int containerDepth =
        depth + (node.type == MD_BLOCK_QUOTE ? 1 : 0) +
        ((node.type == MD_BLOCK_UL || node.type == MD_BLOCK_OL) ? 1 : 0);
    for (auto child : node.children) {
      QString marker;
      if (node.type == MD_BLOCK_UL) {
        marker = QStringLiteral("- ");
      } else if (node.type == MD_BLOCK_OL) {
        marker = QString::number(number++) + QStringLiteral(". ");
      }
      renderBlock(child, cursor, containerDepth, marker, {}, childList,
                  childQuote);
    }
    return;
  }
  if (node.type == MD_BLOCK_LI) {
    item = index;
    if (node.attributes["task"].toBool()) {
      task = index;
      prefix += node.attributes["checked"].toBool() ? QStringLiteral("[x] ")
                                                    : QStringLiteral("[ ] ");
    }
    const auto quoteFormat = [this, quote]() {
      QTextCharFormat format;
      if (quote.has_value()) {
        if (!style_.quoteFontFamily.trimmed().isEmpty()) {
          format.setFontFamilies(QStringList{style_.quoteFontFamily});
        }
        format.setFontItalic(style_.quoteItalic);
        format.setFontPointSize(std::clamp(style_.quoteFontSize, 6, 72));
      }
      return format;
    };
    bool first = true;
    bool inlineOpen = false;
    int inlineStart = 0;
    for (size_t childIndex = 0;
         childIndex < node.children.size(); ++childIndex) {
      const auto child = node.children[childIndex];
      const auto& childNode = document_->nodes[child];
      if (childNode.kind != markdown::NodeKind::kBlock) {
        auto format = quoteFormat();
        if (!inlineOpen) {
          beginBlock(cursor, depth, true);
          inlineStart = cursor.position();
          if (!prefix.isEmpty() && first) {
            const auto taskAt = taskMarkerPosition(prefix);
            const auto markerPart =
                taskAt >= 0 ? prefix.left(taskAt) : prefix;
            QTextCharFormat marker = format;
            marker.setToolTip(QStringLiteral(
                "List marker is protected; delete it to detach the item."));
            if (!markerPart.isEmpty()) {
              const int markerStart = cursor.position();
              cursor.insertText(markerPart, marker);
              if (item) {
                markerRuns_.push_back(
                    {*item, markerStart, int(markerPart.size())});
              }
            }
            const auto rest = prefix.mid(markerPart.size());
            if (!rest.isEmpty()) {
              QTextCharFormat taskFormat = format;
              taskFormat.setToolTip(QStringLiteral(
                  "Task marker is protected; use Toggle task checked."));
              cursor.insertText(rest, taskFormat);
            }
          }
          inlineOpen = true;
        }
        renderInline(child, cursor, format);
        first = false;
        continue;
      }
      if (inlineOpen) {
        contexts_.push_back(
            {inlineStart, cursor.position(), {}, task, index, list, quote});
        inlineOpen = false;
      }
      renderBlock(child, cursor, depth, first ? prefix : QString{}, task, list,
                  quote);
      first = false;
    }
    if (inlineOpen) {
      contexts_.push_back(
          {inlineStart, cursor.position(), {}, task, index, list, quote});
    }
    return;
  }
  if (node.type == MD_BLOCK_P || node.type == MD_BLOCK_H) {
    beginBlock(cursor, depth,
               node.type == MD_BLOCK_P || node.type == MD_BLOCK_LI);
    const int start = cursor.position();
    QTextCharFormat format;
    if (node.type == MD_BLOCK_H) {
      const int level = std::clamp(node.attributes["level"].toInt(), 1, 6);
      applyHeadingStyle(format, style_, level);
    } else if (quote.has_value()) {
      if (!style_.quoteFontFamily.trimmed().isEmpty()) {
        format.setFontFamilies(QStringList{style_.quoteFontFamily});
      }
      format.setFontItalic(style_.quoteItalic);
      format.setFontPointSize(std::clamp(style_.quoteFontSize, 6, 72));
    }
    if (!prefix.isEmpty()) {
      const auto taskAt = taskMarkerPosition(prefix);
      const auto markerPart = taskAt >= 0 ? prefix.left(taskAt) : prefix;
      QTextCharFormat marker = format;
      marker.setToolTip(QStringLiteral(
          "List marker is protected; delete it to detach the item."));
      if (!markerPart.isEmpty()) {
        const int markerStart = cursor.position();
        cursor.insertText(markerPart, marker);
        if (item) {
          markerRuns_.push_back({*item, markerStart, int(markerPart.size())});
        }
      }
      const auto rest = prefix.mid(markerPart.size());
      if (!rest.isEmpty()) {
        QTextCharFormat taskFormat = format;
        taskFormat.setToolTip(QStringLiteral(
            "Task marker is protected; use Toggle task checked."));
        cursor.insertText(rest, taskFormat);
      }
    }
    for (auto child : node.children) {
      renderInline(child, cursor, format);
    }
    std::optional<size_t> heading;
    if (mappedBlocks_[index]) {
      heading = index;
    }
    contexts_.push_back({start, cursor.position(), heading, task, index, list,
                         quote});
    return;
  }
  if (node.type == MD_BLOCK_CODE) {
    renderCodeBlock(index, cursor, depth, list, quote);
    return;
  }
  if (node.type == MD_BLOCK_TABLE) {
    renderTable(index, cursor, depth);
    return;
  }
  if (node.type == MD_BLOCK_HR) {
    renderHorizontalRule(cursor, depth);
    return;
  }
  QString label = QStringLiteral("unsupported block - edit in Markdown");
  switch (node.type) {
    case MD_BLOCK_HTML:
      label = QStringLiteral("HTML - edit in Markdown");
      break;
    case MD_BLOCK_HR:
      label = QStringLiteral("horizontal rule - edit in Markdown");
      break;
    default:
      break;
  }
  opaque(cursor, label, depth);
}

void ContinuousEditor::renderInline(size_t index, QTextCursor& cursor,
                                    QTextCharFormat format, bool allowEditing) {
  const auto& node = document_->nodes[index];
  const auto palette = QApplication::palette();
  if (node.kind == markdown::NodeKind::kSpan) {
    if (node.type == MD_SPAN_CODE) {
      bool editable = allowEditing && node.children.size() == 1;
      if (editable) {
        auto ancestor = node.parent;
        while (ancestor) {
          const auto& parent = document_->nodes[*ancestor];
          if (parent.kind == markdown::NodeKind::kSpan &&
              (parent.type == MD_SPAN_A || parent.type == MD_SPAN_IMG)) {
            editable = false;
            break;
          }
          ancestor = parent.parent;
        }
      }
      if (editable) {
        const auto child = node.children.front();
        const auto& text = document_->nodes[child];
        if (text.kind == markdown::NodeKind::kText &&
            text.type == MD_TEXT_CODE && text.source &&
            text.source->offset >= 0 &&
            text.source->offset <= document_->source.size() &&
            text.source->size == text.text.size() &&
            text.source->size <= document_->source.size() - text.source->offset &&
            document_->source.mid(text.source->offset, text.source->size) ==
                text.text &&
            !text.text.contains('\n') && !text.text.contains('\r')) {
          const auto value = literal(text.text);
          if (!value.isEmpty() && !value.contains('\n') &&
              !value.contains('\r') &&
              !value.contains(QChar::ParagraphSeparator) &&
              !value.contains(QChar::LineSeparator)) {
            auto codeFormat = format;
            codeFormat.setFont(configuredCodeFont(style_));
            codeFormat.setForeground(codeForeground(palette));
            codeFormat.setBackground(codeBackground(palette));
            codeFormat.setToolTip(QStringLiteral("Editable inline code"));
            const int start = cursor.position();
            cursor.insertText(value, codeFormat);
            codeSpanRuns_.push_back({child, start, int(value.size())});
            return;
          }
        }
      }
      QTextCharFormat protectedFormat;
      protectedFormat.setForeground(mutedForeground(palette));
      protectedFormat.setBackground(mutedBackground(palette));
      cursor.insertText(QStringLiteral("[Read-only: code]"), protectedFormat);
      return;
    }
    if (node.type == MD_SPAN_EM) {
      format.setFontItalic(true);
    } else if (node.type == MD_SPAN_STRONG) {
      format.setFontWeight(QFont::Bold);
    } else if (node.type == MD_SPAN_DEL) {
      format.setFontStrikeOut(true);
    } else if (node.type == MD_SPAN_A) {
      format.setForeground(linkForeground(palette));
      format.setToolTip(QStringLiteral(
          "Link - navigation disabled. Edit label text only; use Markdown for "
          "addresses and reference definitions."));
      allowEditing = allowEditing && !node.attributes["autolink"].toBool();
      if (!allowEditing) {
        format.setToolTip(
            QStringLiteral("Read-only autolink - text determines the address. "
                           "Navigation disabled; edit in Markdown."));
      }
    } else if (node.type == MD_SPAN_IMG) {
      format.setForeground(imageForeground(palette));
      format.setBackground(imageBackground(palette));
      format.setToolTip(QStringLiteral(
          "Image - resource loading disabled. Edit alt text only; use "
          "Markdown for source, title and reference definitions."));
    } else {
      QTextCharFormat protectedFormat;
      protectedFormat.setForeground(mutedForeground(palette));
      protectedFormat.setBackground(mutedBackground(palette));
      const QString label = node.type == MD_SPAN_IMG ? QStringLiteral("image")
                            : node.type == MD_SPAN_CODE
                                ? QStringLiteral("code")
                                : QStringLiteral("span");
      cursor.insertText(QStringLiteral("[Read-only: ") + label + ']',
                        protectedFormat);
      return;
    }
    for (auto child : node.children) {
      renderInline(child, cursor, format, allowEditing);
    }
    return;
  }
  if (node.kind != markdown::NodeKind::kText) {
    return;
  }
  if (node.type == MD_TEXT_SOFTBR || node.type == MD_TEXT_BR) {
    QTextCharFormat breakFormat = format;
    breakFormat.setToolTip(QStringLiteral(
        "Line break; delete it to join the lines."));
    const auto glyph = node.type == MD_TEXT_BR
                           ? QString(QChar::LineSeparator)
                           : QStringLiteral(" ");
    const int start = cursor.position();
    cursor.insertText(glyph, breakFormat);
    breakRuns_.push_back({index, start, 1});
    return;
  }
  QString text = literal(node.text);
  const bool canEdit = allowEditing && editable(*document_, node);
  if (!canEdit) {
    format.setForeground(mutedForeground(palette));
    format.setBackground(mutedBackground(palette));
    if (node.type == MD_TEXT_HTML) {
      text = QStringLiteral("[Read-only: inline HTML]");
    }
  } else {
    if (format.toolTip().isEmpty()) {
      format.setToolTip(QStringLiteral("Editable text run"));
    }
  }
  const int start = cursor.position();
  cursor.insertText(text, format);
  if (canEdit) {
    runs_.push_back({index, start, int(text.size())});
  }
}

void ContinuousEditor::restore(const QString& reason) {
  const auto cursor = editor_->textCursor().position();
  const DocumentSnapshot snapshot{
      document_ ? document_->source.toStdString() : std::string{}, {}, token_};
  render(snapshot);
  auto restored = editor_->textCursor();
  restored.setPosition(
      std::min(cursor, editor_->document()->characterCount() - 1));
  editor_->setTextCursor(restored);
  if (!reason.isEmpty()) {
    emit errorOccurred(reason);
  }
}

void ContinuousEditor::changed() {
  if (rendering_ || !document_) {
    return;
  }
  const auto change = change_;
  change_.reset();
  const auto text = rawText(editor_);
  if (change && text == renderedText_) {
    restore({});
    return;
  }
  if (!change) {
    restore(
        QStringLiteral("Formatting is protected in this slice. Use Markdown."));
    return;
  }
  if (change->position < 0 || change->removed < 0 || change->added < 0 ||
      change->position > renderedText_.size() ||
      change->position > text.size() ||
      change->removed > renderedText_.size() - change->position ||
      change->added > text.size() - change->position ||
      text.left(change->position) != renderedText_.left(change->position) ||
      text.mid(change->position + change->added) !=
          renderedText_.mid(change->position + change->removed)) {
    restore(QStringLiteral("Cannot map this edit. Use Markdown."));
    return;
  }
  const auto overlaps = [&](int start, int length) {
    const int offset = change->position - start;
    return offset >= 0 && offset <= length &&
           change->removed <= length - offset;
  };
  const auto overlapsEditable = [&] {
    for (const auto& run : codeRuns_) {
      if (overlaps(run.start, run.length)) {
        return true;
      }
    }
    for (const auto& run : runs_) {
      if (overlaps(run.start, run.length)) {
        return true;
      }
    }
    for (const auto& run : codeSpanRuns_) {
      if (overlaps(run.start, run.length)) {
        return true;
      }
    }
    for (const auto& run : emptyCellRuns_) {
      if (overlaps(run.start, 0)) {
        return true;
      }
    }
    return false;
  };
  for (const auto& run : breakRuns_) {
    if (!overlaps(run.start, run.length)) {
      continue;
    }
    if (overlapsEditable()) {
      restore(QStringLiteral(
          "Delete the line break separately from the surrounding text."));
      return;
    }
    if (change->added == 0 && change->removed > 0) {
      emit commandRequested(token_, markdown::RemoveLineBreak{run.node},
                            editor_->textCursor().position());
      return;
    }
    restore(QStringLiteral(
        "Line breaks are edited by deleting the break character."));
    return;
  }
  for (const auto& run : markerRuns_) {
    if (!overlaps(run.start, run.length)) {
      continue;
    }
    if (overlapsEditable()) {
      restore(QStringLiteral(
          "Delete the list marker separately from the item text."));
      return;
    }
    if (change->added == 0 && change->removed > 0) {
      emit commandRequested(token_, markdown::DetachListItem{run.item},
                            editor_->textCursor().position());
      return;
    }
    restore(QStringLiteral(
        "List markers are edited by deleting the marker."));
    return;
  }
  const Run* target = nullptr;
  const CodeRun* codeTarget = nullptr;
  const CodeSpanRun* codeSpanTarget = nullptr;
  const EmptyCellRun* emptyCellTarget = nullptr;
  for (const auto& run : codeRuns_) {
    const int offset = change->position - run.start;
    if (offset < 0 || offset > run.length ||
        change->removed > run.length - offset) {
      continue;
    }
    if (target || codeTarget) {
      restore(
          QStringLiteral("This insertion is between text runs. Move inside a "
                         "run or use Markdown."));
      return;
    }
    codeTarget = &run;
  }
  for (const auto& run : runs_) {
    const int offset = change->position - run.start;
    if (offset < 0 || offset > run.length ||
        change->removed > run.length - offset) {
      continue;
    }
    if (target || codeTarget) {
      restore(
          QStringLiteral("This insertion is between text runs. Move inside a "
                         "run or use Markdown."));
      return;
    }
    target = &run;
  }
  for (const auto& run : emptyCellRuns_) {
    if (change->position != run.start || change->removed != 0 ||
        change->added <= 0) {
      continue;
    }
    if (target || codeTarget || codeSpanTarget || emptyCellTarget) {
      restore(
          QStringLiteral("This insertion is between editable targets. Move "
                         "inside one cell or use Markdown."));
      return;
    }
    emptyCellTarget = &run;
  }
  for (const auto& run : codeSpanRuns_) {
    const int offset = change->position - run.start;
    if (offset < 0 || offset > run.length ||
        change->removed > run.length - offset) {
      continue;
    }
    if (target || codeTarget || codeSpanTarget || emptyCellTarget) {
      restore(
          QStringLiteral("This insertion is between editable targets. Move "
                         "inside one cell or use Markdown."));
      return;
    }
    codeSpanTarget = &run;
  }
  if (codeSpanTarget) {
    auto replacement =
        renderedText_.mid(codeSpanTarget->start, codeSpanTarget->length);
    replacement.replace(change->position - codeSpanTarget->start,
                        change->removed,
                        text.mid(change->position, change->added));
    if (replacement.isEmpty() || !replacement.isValidUtf16() ||
        replacement.contains('\n') || replacement.contains('\r') ||
        replacement.contains(QChar::ParagraphSeparator) ||
        replacement.contains(QChar::LineSeparator) ||
        replacement.contains(QChar(0))) {
      restore(QStringLiteral(
          "Inline code accepts nonempty single-line text. Use Markdown."));
      return;
    }
    const auto command =
        markdown::ReplaceInlineCode{codeSpanTarget->node,
                                    replacement.toUtf8()};
    emit commandRequested(token_, command, editor_->textCursor().position());
    return;
  }
  if (emptyCellTarget) {
    const auto replacement = text.mid(change->position, change->added);
    if (replacement.isEmpty() || !replacement.isValidUtf16() ||
        replacement.contains('\n') || replacement.contains('\r') ||
        replacement.contains(QChar::ParagraphSeparator) ||
        replacement.contains(QChar::LineSeparator) ||
        replacement.contains(QChar(0))) {
      restore(QStringLiteral(
          "Empty table cells accept single-line plain text. Use Markdown."));
      return;
    }
    const auto command = markdown::InsertTableCellText{
        emptyCellTarget->node, replacement.toUtf8()};
    emit commandRequested(token_, command, editor_->textCursor().position());
    return;
  }
  if (codeTarget) {
    auto replacement =
        renderedText_.mid(codeTarget->start, codeTarget->length);
    replacement.replace(change->position - codeTarget->start, change->removed,
                        text.mid(change->position, change->added));
    replacement.replace(QChar::LineSeparator, QChar('\n'));
    replacement.replace(QChar::ParagraphSeparator, QChar('\n'));
    if (!replacement.isValidUtf16() || replacement.contains(QChar(0))) {
      restore(QStringLiteral(
          "Code edits must be valid Unicode without NUL. Use Markdown."));
      return;
    }
    const auto command =
        markdown::ReplaceCodeBody{codeTarget->node, replacement.toUtf8()};
    emit commandRequested(token_, command, editor_->textCursor().position());
    return;
  }
  if (target) {
    auto replacement = renderedText_.mid(target->start, target->length);
    replacement.replace(change->position - target->start, change->removed,
                        text.mid(change->position, change->added));
    if (replacement.isEmpty() || !replacement.isValidUtf16() ||
        replacement.contains('\n') || replacement.contains('\r') ||
        replacement.contains(QChar::ParagraphSeparator) ||
        replacement.contains(QChar::LineSeparator) ||
        replacement.contains(QChar(0))) {
      restore(QStringLiteral(
          "Only nonempty single-line Unicode text is editable. Use Markdown."));
      return;
    }
    const auto command =
        markdown::ReplaceText{target->node, replacement.toUtf8()};
    emit commandRequested(token_, command, editor_->textCursor().position());
    return;
  }
  restore(
      QStringLiteral("This fragment or selection is protected. Use Markdown."));
}

markdown::ParseResult ContinuousEditor::apply(
    const markdown::EditCommand& command) const {
  if (!document_) {
    return {std::nullopt, QStringLiteral("No rendered document")};
  }
  return markdown::applyEdit(*document_, markdown::snapshotToken(*document_),
                             command);
}

std::optional<qsizetype> ContinuousEditor::sourcePosition(int position,
                                                          bool forward) const {
  std::optional<qsizetype> boundary;
  if (document_) {
    for (const auto& run : runs_) {
      if (position >= run.start && position <= run.start + run.length) {
        const auto& node = document_->nodes[run.node];
        const auto text = literal(node.text);
        const int offset = position - run.start;
        if (offset < text.size() && text[offset].isLowSurrogate()) {
          return std::nullopt;
        }
        const auto source =
            node.source->offset + text.left(offset).toUtf8().size();
        if ((forward && offset < run.length) || (!forward && offset > 0)) {
          return source;
        }
        boundary = source;
      }
    }
  }
  return boundary;
}

int ContinuousEditor::viewPosition(qsizetype source, bool* exact) const {
  *exact = false;
  int nearest = 0;
  qsizetype distance = std::numeric_limits<qsizetype>::max();
  if (document_) {
    for (const auto& run : runs_) {
      const auto& node = document_->nodes[run.node];
      if (source >= node.source->offset &&
          source <= node.source->offset + node.source->size) {
        const auto prefix = node.text.left(source - node.source->offset);
        if (literal(prefix).toUtf8() == prefix) {
          *exact = true;
          return run.start + literal(prefix).size();
        }
      }
      for (bool end : {false, true}) {
        const auto boundary =
            node.source->offset + (end ? node.source->size : 0);
        const auto delta =
            source >= boundary ? source - boundary : boundary - source;
        if (delta < distance) {
          distance = delta;
          nearest = run.start + (end ? run.length : 0);
        }
      }
    }
  }
  return nearest;
}

const ContinuousEditor::Context* ContinuousEditor::context() const {
  const auto cursor = editor_->textCursor();
  for (const auto& value : contexts_) {
    if (cursor.selectionStart() >= value.start &&
        cursor.selectionEnd() <= value.end) {
      return &value;
    }
  }
  return nullptr;
}

std::optional<size_t> ContinuousEditor::headingNode() const {
  const auto* value = context();
  return value ? value->heading : std::nullopt;
}

std::optional<size_t> ContinuousEditor::taskNode() const {
  const auto* value = context();
  return value ? value->task : std::nullopt;
}

bool ContinuousEditor::taskChecked() const {
  const auto node = taskNode();
  return document_ && node &&
         document_->nodes[*node].attributes["checked"].toBool();
}

ContinuousEditor::InlineState ContinuousEditor::inlineState(
    markdown::InlineStyle style) const {
  const auto cursor = editor_->textCursor();
  if (!document_ || !cursor.hasSelection()) {
    return {{},
            false,
            QStringLiteral("Select text inside one editable run first.")};
  }
  for (const auto& run : runs_) {
    if (cursor.selectionStart() < run.start ||
        cursor.selectionEnd() > run.start + run.length) {
      continue;
    }
    const auto& node = document_->nodes[run.node];
    const auto text = literal(node.text);
    const auto first = cursor.selectionStart() - run.start;
    const auto end = cursor.selectionEnd() - run.start;
    if ((first < text.size() && text[first].isLowSurrogate()) ||
        (end < text.size() && text[end].isLowSurrogate())) {
      return {
          {}, false, QStringLiteral("Selection splits a Unicode character.")};
    }
    const auto offset = text.left(first).toUtf8().size();
    const auto size = text.mid(first, end - first).toUtf8().size();
    const markdown::ToggleInlineStyle command{run.node, {offset, size}, style};
    bool checked = false;
    const int type = style == markdown::InlineStyle::kBold     ? MD_SPAN_STRONG
                     : style == markdown::InlineStyle::kItalic ? MD_SPAN_EM
                                                               : MD_SPAN_DEL;
    auto ancestor = node.parent;
    while (ancestor) {
      const auto& parent = document_->nodes[*ancestor];
      checked = checked || (parent.kind == markdown::NodeKind::kSpan &&
                            parent.type == type);
      ancestor = parent.parent;
    }
    const auto result = apply(command);
    if (!result.document) {
      return {{}, checked, result.error};
    }
    return {command, checked, {}};
  }
  return {{},
          false,
          QStringLiteral("Inline formatting requires selection inside one "
                         "editable text run.")};
}

void ContinuousEditor::toggleInlineStyle(markdown::InlineStyle style) {
  const auto state = inlineState(style);
  if (!state.command) {
    emit errorOccurred(state.reason);
    return;
  }
  emit commandRequested(token_, *state.command,
                        editor_->textCursor().position());
}

std::optional<ContinuousEditor::LinkSelection>
ContinuousEditor::linkSelection() const {
  if (!document_) {
    return std::nullopt;
  }
  const auto cursor = editor_->textCursor();
  if (!cursor.hasSelection()) {
    return std::nullopt;
  }
  for (const auto& run : runs_) {
    if (cursor.selectionStart() < run.start ||
        cursor.selectionEnd() > run.start + run.length) {
      continue;
    }
    const auto& node = document_->nodes[run.node];
    if (node.kind != markdown::NodeKind::kText ||
        node.type != MD_TEXT_NORMAL) {
      return std::nullopt;
    }
    const auto text = literal(node.text);
    const auto first = cursor.selectionStart() - run.start;
    const auto end = cursor.selectionEnd() - run.start;
    if (first >= end ||
        (first < text.size() && text[first].isLowSurrogate()) ||
        (end < text.size() && text[end].isLowSurrogate())) {
      return std::nullopt;
    }
    const auto offset = text.left(first).toUtf8().size();
    const auto size = text.mid(first, end - first).toUtf8().size();
    if (offset < 0 || size <= 0) {
      return std::nullopt;
    }
    return LinkSelection{run.node, {offset, size}};
  }
  return std::nullopt;
}

bool ContinuousEditor::linkCreationEnabled() const {
  const auto selection = linkSelection();
  if (!selection) {
    return false;
  }
  return apply(markdown::WrapInLink{selection->node, selection->range,
                                    "https://example.invalid"})
      .document.has_value();
}

void ContinuousEditor::insertLink(const QString& url) {
  const auto selection = linkSelection();
  if (!selection) {
    emit errorOccurred(
        QStringLiteral("Select text inside one editable run to create a link."));
    return;
  }
  if (url.trimmed().isEmpty()) {
    emit errorOccurred(QStringLiteral("Link URL must not be empty."));
    return;
  }
  const auto command = markdown::WrapInLink{
      selection->node, selection->range, url.trimmed().toUtf8()};
  emit commandRequested(token_, command, editor_->textCursor().position());
}

bool ContinuousEditor::imageCreationEnabled() const {
  const auto selection = linkSelection();
  if (!selection) {
    return false;
  }
  return apply(markdown::InsertImage{selection->node, selection->range,
                                     "https://example.invalid/image.png"})
      .document.has_value();
}

void ContinuousEditor::insertImage(const QString& src) {
  const auto selection = linkSelection();
  if (!selection) {
    emit errorOccurred(
        QStringLiteral("Select alt text inside one editable run to insert an "
                       "image."));
    return;
  }
  if (src.trimmed().isEmpty()) {
    emit errorOccurred(QStringLiteral("Image source must not be empty."));
    return;
  }
  const auto command = markdown::InsertImage{
      selection->node, selection->range, src.trimmed().toUtf8()};
  emit commandRequested(token_, command, editor_->textCursor().position());
}

ContinuousEditor::StructuralState ContinuousEditor::blockquoteState() const {
  const auto* value = context();
  if (!document_ || !value) {
    return {{},
            false,
            QStringLiteral("Blockquote requires the cursor inside one "
                           "editable block.")};
  }
  if (value->quote) {
    const auto command = markdown::UnwrapQuote{*value->quote};
    const auto result = apply(command);
    if (!result.document) {
      return {{}, true, result.error};
    }
    return {command, true, {}};
  }
  auto target = value->block;
  if (document_->nodes[target].type == MD_BLOCK_LI && value->list) {
    target = *value->list;
  }
  const auto& block = document_->nodes[target];
  const auto parent = block.parent.value_or(0);
  if (parent >= document_->nodes.size()) {
    return {{}, false, QStringLiteral("Invalid blockquote parent.")};
  }
  const auto& container = document_->nodes[parent];
  if (container.kind != markdown::NodeKind::kBlock ||
      (container.type != MD_BLOCK_DOC && container.type != MD_BLOCK_QUOTE &&
       container.type != MD_BLOCK_LI)) {
    return {{},
            false,
            QStringLiteral("Blockquote requires a paragraph, heading, list or "
                           "code block; use Markdown for this block.")};
  }
  const auto position =
      std::find(container.children.begin(), container.children.end(), target);
  if (position == container.children.end()) {
    return {{}, false, QStringLiteral("Invalid blockquote target.")};
  }
  const auto command = markdown::WrapInQuote{
      parent, size_t(position - container.children.begin()), size_t{1}};
  const auto result = apply(command);
  if (!result.document) {
    return {{}, false, result.error};
  }
  return {command, false, {}};
}

ContinuousEditor::StructuralState ContinuousEditor::listState(
    bool ordered) const {
  const auto* value = context();
  if (!document_ || !value) {
    return {{},
            false,
            QStringLiteral("Lists require the cursor inside one editable "
                           "block.")};
  }
  if (value->list) {
    const auto& list = document_->nodes[*value->list];
    if (list.kind != markdown::NodeKind::kBlock ||
        (list.type != MD_BLOCK_UL && list.type != MD_BLOCK_OL)) {
      return {{}, false, QStringLiteral("Invalid list target.")};
    }
    const bool currentOrdered = list.type == MD_BLOCK_OL;
    const int start =
        ordered ? (currentOrdered ? list.attributes["start"].toInt() : 1) : 0;
    const auto command = markdown::SetListKind{*value->list, ordered, start};
    const auto result = apply(command);
    if (!result.document) {
      return {{}, currentOrdered == ordered, result.error};
    }
    return {command, currentOrdered == ordered, {}};
  }
  if (value->quote) {
    return {{},
            false,
            QStringLiteral("List creation inside a blockquote is not "
                           "supported yet; use Markdown.")};
  }
  const auto& block = document_->nodes[value->block];
  if (block.kind != markdown::NodeKind::kBlock ||
      block.type != MD_BLOCK_P ||
      block.parent != std::optional<size_t>(0)) {
    return {{},
            false,
            QStringLiteral("List creation requires a top-level paragraph; use "
                           "Markdown for this block.")};
  }
  const auto& root = document_->nodes[0];
  const auto position =
      std::find(root.children.begin(), root.children.end(), value->block);
  if (position == root.children.end()) {
    return {{}, false, QStringLiteral("Invalid list target.")};
  }
  const auto command =
      markdown::WrapInList{0, size_t(position - root.children.begin()), ordered,
                           ordered ? 1 : 0};
  const auto result = apply(command);
  if (!result.document) {
    return {{}, false, result.error};
  }
  return {command, false, {}};
}

void ContinuousEditor::toggleBlockquote() {
  const auto state = blockquoteState();
  if (!state.command) {
    emit errorOccurred(state.reason);
    return;
  }
  emit commandRequested(token_, *state.command,
                        editor_->textCursor().position());
}

void ContinuousEditor::changeListKind(bool ordered) {
  const auto state = listState(ordered);
  if (!state.command) {
    emit errorOccurred(state.reason);
    return;
  }
  emit commandRequested(token_, *state.command,
                        editor_->textCursor().position());
}

std::optional<ContinuousEditor::TableContext>
ContinuousEditor::tableContext() const {
  if (!document_ || !editor_) {
    return std::nullopt;
  }
  auto* table = editor_->textCursor().currentTable();
  if (!table) {
    return std::nullopt;
  }
  for (const auto& run : tableRuns_) {
    if (run.widget != table) {
      continue;
    }
    const auto cell = table->cellAt(editor_->textCursor());
    if (!cell.isValid()) {
      return std::nullopt;
    }
    return TableContext{run.node, cell.row(), cell.column(), table->rows(),
                        table->columns()};
  }
  return std::nullopt;
}

std::optional<markdown::EditCommand> ContinuousEditor::tableCommand(
    const TableContext& context, TableAction action) const {
  const auto emptyCells = [](int count) {
    return std::vector<QByteArray>(size_t(std::max(0, count)));
  };
  switch (action) {
    case TableAction::kInsertRowAbove:
      if (context.row <= 0) {
        return std::nullopt;
      }
      return markdown::InsertTableRow{
          context.node, size_t(context.row - 1),
          emptyCells(context.columns)};
    case TableAction::kInsertRowBelow:
      return markdown::InsertTableRow{
          context.node, size_t(std::max(0, context.row)),
          emptyCells(context.columns)};
    case TableAction::kRemoveRow:
      if (context.row <= 0) {
        return std::nullopt;
      }
      return markdown::RemoveTableRow{context.node,
                                      size_t(context.row - 1)};
    case TableAction::kInsertColumnLeft:
      return markdown::InsertTableColumn{
          context.node, size_t(std::max(0, context.column)), {},
          emptyCells(context.rows - 1), markdown::TableAlignment::kNone};
    case TableAction::kInsertColumnRight:
      return markdown::InsertTableColumn{
          context.node, size_t(std::max(0, context.column + 1)), {},
          emptyCells(context.rows - 1), markdown::TableAlignment::kNone};
    case TableAction::kRemoveColumn:
      if (context.columns <= 1) {
        return std::nullopt;
      }
      return markdown::RemoveTableColumn{context.node,
                                         size_t(context.column)};
    case TableAction::kAlignLeft:
      return markdown::SetTableAlignment{
          context.node, size_t(context.column),
          markdown::TableAlignment::kLeft};
    case TableAction::kAlignCenter:
      return markdown::SetTableAlignment{
          context.node, size_t(context.column),
          markdown::TableAlignment::kCenter};
    case TableAction::kAlignRight:
      return markdown::SetTableAlignment{
          context.node, size_t(context.column),
          markdown::TableAlignment::kRight};
    case TableAction::kRemoveTable:
      return markdown::RemoveTable{context.node};
  }
  return std::nullopt;
}

bool ContinuousEditor::tableActionEnabled(TableAction action) const {
  const auto context = tableContext();
  return context && tableCommand(*context, action).has_value();
}

void ContinuousEditor::applyTableAction(TableAction action) {
  const auto context = tableContext();
  if (!context) {
    emit errorOccurred(
        QStringLiteral("Place the cursor inside a rendered table first."));
    return;
  }
  const auto command = tableCommand(*context, action);
  if (!command) {
    emit errorOccurred(
        QStringLiteral("This table action is not available for the current "
                       "cell."));
    return;
  }
  emit commandRequested(token_, *command, editor_->textCursor().position());
}

bool ContinuousEditor::blockInsertionEnabled() const {
  return document_ && !document_->nodes.empty();
}

bool ContinuousEditor::tableCreationEnabled() const {
  return blockInsertionEnabled();
}

size_t ContinuousEditor::insertionPosition() const {
  if (!document_ || document_->nodes.empty()) {
    return 0;
  }
  const auto& root = document_->nodes[0];
  size_t position = root.children.size();
  std::optional<size_t> top;
  if (const auto table = tableContext()) {
    top = table->node;
  } else if (const auto* value = context()) {
    top = value->block;
    while (top && document_->nodes[*top].parent &&
           *document_->nodes[*top].parent != 0) {
      top = document_->nodes[*top].parent;
    }
  }
  if (top) {
    const auto found =
        std::find(root.children.begin(), root.children.end(), *top);
    if (found != root.children.end()) {
      position = size_t(found - root.children.begin()) + 1;
    }
  }
  return position;
}

void ContinuousEditor::insertTable(size_t columns, size_t bodyRows) {
  if (!blockInsertionEnabled()) {
    emit errorOccurred(QStringLiteral("No document is available."));
    return;
  }
  const auto command = markdown::InsertTable{insertionPosition(), columns,
                                             bodyRows};
  emit commandRequested(token_, command, editor_->textCursor().position());
}

void ContinuousEditor::insertHorizontalRule() {
  if (!blockInsertionEnabled()) {
    emit errorOccurred(QStringLiteral("No document is available."));
    return;
  }
  const auto command = markdown::InsertHorizontalRule{insertionPosition()};
  emit commandRequested(token_, command, editor_->textCursor().position());
}

void ContinuousEditor::insertFencedCode() {
  if (!blockInsertionEnabled()) {
    emit errorOccurred(QStringLiteral("No document is available."));
    return;
  }
  const auto command = markdown::InsertFencedCode{insertionPosition()};
  emit commandRequested(token_, command, editor_->textCursor().position());
}

void ContinuousEditor::explainCursorMapping(bool exact) {
  if (!exact) {
    emit noticeMessage(
        QStringLiteral("Cursor/selection moved to the nearest mapped boundary: "
                       "this source position has no editable formatted "
                       "counterpart."));
  }
}

bool ContinuousEditor::eventFilter(QObject* watched, QEvent* event) {
  if (watched == editor_ && (event->type() == QEvent::KeyPress ||
                             event->type() == QEvent::ShortcutOverride)) {
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->matches(QKeySequence::Bold) ||
        key->matches(QKeySequence::Italic)) {
      if (event->type() == QEvent::KeyPress) {
        toggleInlineStyle(key->matches(QKeySequence::Bold)
                              ? markdown::InlineStyle::kBold
                              : markdown::InlineStyle::kItalic);
      }
      event->accept();
      return true;
    }
    if (key->matches(QKeySequence::Underline)) {
      if (event->type() == QEvent::KeyPress) {
        emit errorOccurred(
            QStringLiteral("Underline is not supported by GFM."));
      }
      event->accept();
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

void ContinuousEditor::setNormalLineWrapMode(QTextEdit::LineWrapMode mode) {
  editor_->setNormalLineWrapMode(mode);
}

void ContinuousEditor::setShowSpecialCharacters(bool show) {
  showSpecialCharacters_ = show;
  editor_->setShowSpecialCharacters(show);
  canvas_->setShowSpecialCharacters(show);
}

void ContinuousEditor::setPaged(bool paged) {
  if (paged_ == paged) {
    return;
  }
  paged_ = paged;
  if (!canvas_) {
    return;
  }
  if (paged) {
    editor_->setPaged(false);
    canvas_->setDocument(editor_->document());
    canvas_->setPageSetup(pageSetup_);
    canvas_->setViewScale(editor_->viewScale());
    canvas_->setLayoutSuspended(false);
    editor_->hide();
    canvas_->show();
    canvas_->refreshLayout();
    canvas_->setTextCursor(editor_->textCursor());
  } else {
    if (!canvas_->textCursor().isNull()) {
      editor_->setTextCursor(canvas_->textCursor());
    }
    canvas_->hide();
    editor_->show();
    editor_->setPaged(false);
  }
}

void ContinuousEditor::setPageSetup(const PageSetup& setup) {
  pageSetup_ = setup;
  editor_->setPageSetup(setup);
  if (canvas_) {
    canvas_->setPageSetup(setup);
  }
}

QWidget* ContinuousEditor::inputWidget() const {
  return paged_ && canvas_ ? static_cast<QWidget*>(canvas_)
                           : static_cast<QWidget*>(editor_);
}

void ContinuousEditor::focusInput() {
  inputWidget()->setFocus(Qt::OtherFocusReason);
}

void ContinuousEditor::syncCursorToCanvas() {
  if (!paged_ || !canvas_ || syncingCursor_) {
    return;
  }
  syncingCursor_ = true;
  canvas_->setTextCursor(editor_->textCursor());
  syncingCursor_ = false;
}

void ContinuousEditor::syncCursorToEditor() {
  if (!paged_ || !canvas_ || syncingCursor_) {
    return;
  }
  syncingCursor_ = true;
  editor_->setTextCursor(canvas_->textCursor());
  syncingCursor_ = false;
}

int ContinuousEditor::pageCount() const {
  return paged_ && canvas_ ? canvas_->pageCount() : 0;
}

int ContinuousEditor::currentPage() const {
  return paged_ && canvas_ ? canvas_->currentPage() : 0;
}

QString ContinuousEditor::layoutWarning() const {
  return paged_ && canvas_ ? canvas_->layoutWarning() : QString{};
}

}  // namespace markpad
