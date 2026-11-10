#include "continuous_editor.h"

#include <QColor>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QSignalBlocker>
#include <QStringConverter>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>

#include "page_text_edit.h"

namespace markpad {
namespace {

const QString kNotice = QStringLiteral(
    "Limited WYSIWYG: edit within a highlighted text run. Grey fragments and "
    "run/block boundaries are protected; use Markdown for other changes. "
    "Links are inert: edit their labels only; use Markdown for addresses.");

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
      notice_(new QLabel(kNotice, this)),
      editor_(new PlainTextOnlyEdit(this)) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  notice_->setWordWrap(true);
  notice_->setObjectName(QStringLiteral("continuousNotice"));
  editor_->setObjectName(QStringLiteral("continuousEditor"));
  layout->addWidget(notice_);
  layout->addWidget(editor_);
  editor_->installEventFilter(this);
  connect(editor_->document(), &QTextDocument::contentsChange, this,
          [this](int position, int removed, int added) {
            if (!rendering_ && (removed || added)) {
              change_ = Change{position, removed, added};
            }
          });
  connect(editor_, &QTextEdit::textChanged, this, &ContinuousEditor::changed);
}

QString ContinuousEditor::render(const DocumentSnapshot& snapshot) {
  const QSignalBlocker blocker(editor_);
  rendering_ = true;
  token_ = snapshot.token;
  auto parsed = markdown::parse(QByteArray::fromStdString(snapshot.source),
                                snapshot.token.revision);
  document_ = std::move(parsed.document);
  runs_.clear();
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
  notice_->setText(kNotice);
  rendering_ = false;
  return parsed.error;
}

void ContinuousEditor::beginBlock(QTextCursor& cursor, int depth) {
  if (!firstBlock_) {
    cursor.insertBlock();
  }
  firstBlock_ = false;
  QTextBlockFormat block;
  block.setLeftMargin(depth * 20);
  block.setBottomMargin(8);
  cursor.setBlockFormat(block);
  cursor.setCharFormat(QTextCharFormat{});
}

void ContinuousEditor::opaque(QTextCursor& cursor, const QString& label,
                              int depth) {
  beginBlock(cursor, depth);
  QTextCharFormat format;
  format.setForeground(QColor(100, 100, 100));
  format.setBackground(QColor(235, 235, 235));
  cursor.insertText(QStringLiteral("[Read-only: ") + label + ']', format);
}

void ContinuousEditor::renderBlock(size_t index, QTextCursor& cursor, int depth,
                                   QString prefix, std::optional<size_t> task) {
  const auto& node = document_->nodes[index];
  if (node.kind != markdown::NodeKind::kBlock) {
    return;
  }
  if (node.type == MD_BLOCK_DOC || node.type == MD_BLOCK_QUOTE ||
      node.type == MD_BLOCK_UL || node.type == MD_BLOCK_OL) {
    qint64 number = node.attributes["start"].toInteger(1);
    for (auto child : node.children) {
      QString marker;
      if (node.type == MD_BLOCK_UL) {
        marker = QStringLiteral("- ");
      } else if (node.type == MD_BLOCK_OL) {
        marker = QString::number(number++) + QStringLiteral(". ");
      }
      renderBlock(child, cursor, depth + (node.type == MD_BLOCK_QUOTE ? 1 : 0),
                  marker);
    }
    return;
  }
  if (node.type == MD_BLOCK_LI) {
    if (node.attributes["task"].toBool()) {
      task = index;
      prefix += node.attributes["checked"].toBool() ? QStringLiteral("[x] ")
                                                    : QStringLiteral("[ ] ");
    }
    const bool hasBlocks = std::any_of(
        node.children.begin(), node.children.end(), [this](size_t child) {
          return document_->nodes[child].kind == markdown::NodeKind::kBlock;
        });
    if (hasBlocks) {
      bool first = true;
      for (auto child : node.children) {
        renderBlock(child, cursor, depth + (first ? 0 : 1),
                    first ? prefix : QString{}, task);
        first = false;
      }
      return;
    }
  }
  if (node.type == MD_BLOCK_P || node.type == MD_BLOCK_H ||
      node.type == MD_BLOCK_LI) {
    beginBlock(cursor, depth);
    const int start = cursor.position();
    QTextCharFormat format;
    if (node.type == MD_BLOCK_H) {
      const int level = std::clamp(node.attributes["level"].toInt(), 1, 6);
      format.setFontWeight(QFont::Bold);
      format.setFontPointSize(24 - level * 2);
    }
    if (!prefix.isEmpty()) {
      QTextCharFormat marker;
      marker.setForeground(QColor(100, 100, 100));
      marker.setBackground(QColor(235, 235, 235));
      cursor.insertText(prefix, marker);
    }
    for (auto child : node.children) {
      renderInline(child, cursor, format);
    }
    std::optional<size_t> heading;
    if (mappedBlocks_[index]) {
      heading = index;
    }
    contexts_.push_back({start, cursor.position(), heading, task});
    return;
  }
  QString label = QStringLiteral("unsupported block - edit in Markdown");
  switch (node.type) {
    case MD_BLOCK_CODE:
      label = QStringLiteral("code - edit in Markdown");
      break;
    case MD_BLOCK_HTML:
      label = QStringLiteral("HTML - edit in Markdown");
      break;
    case MD_BLOCK_TABLE:
      label = QStringLiteral("table - edit in Markdown");
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
  if (node.kind == markdown::NodeKind::kSpan) {
    if (node.type == MD_SPAN_EM) {
      format.setFontItalic(true);
    } else if (node.type == MD_SPAN_STRONG) {
      format.setFontWeight(QFont::Bold);
    } else if (node.type == MD_SPAN_DEL) {
      format.setFontStrikeOut(true);
    } else if (node.type == MD_SPAN_A) {
      format.setForeground(QColor(35, 80, 170));
      format.setToolTip(QStringLiteral(
          "Link - navigation disabled. Edit label text only; use Markdown for "
          "addresses and reference definitions."));
      allowEditing = allowEditing && !node.attributes["autolink"].toBool();
      if (!allowEditing) {
        format.setToolTip(
            QStringLiteral("Read-only autolink - text determines the address. "
                           "Navigation disabled; edit in Markdown."));
      }
    } else {
      QTextCharFormat protectedFormat;
      protectedFormat.setForeground(QColor(100, 100, 100));
      protectedFormat.setBackground(QColor(235, 235, 235));
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
  QString text = literal(node.text);
  const bool canEdit = allowEditing && editable(*document_, node);
  if (!canEdit) {
    format.setForeground(QColor(100, 100, 100));
    format.setBackground(QColor(235, 235, 235));
    if (node.type == MD_TEXT_HTML) {
      text = QStringLiteral("[Read-only: inline HTML]");
    } else if (node.type == MD_TEXT_SOFTBR || node.type == MD_TEXT_BR) {
      text = QStringLiteral(" [Read-only: line break] ");
    }
  } else {
    format.setBackground(QColor(240, 248, 255));
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
  const Run* target = nullptr;
  for (const auto& run : runs_) {
    const int offset = change->position - run.start;
    if (offset < 0 || offset > run.length ||
        change->removed > run.length - offset) {
      continue;
    }
    if (target) {
      restore(
          QStringLiteral("This insertion is between text runs. Move inside a "
                         "run or use Markdown."));
      return;
    }
    target = &run;
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

void ContinuousEditor::explainCursorMapping(bool exact) {
  notice_->setText(
      kNotice + (exact
                     ? QString{}
                     : QStringLiteral(
                           " Cursor/selection moved to the nearest mapped "
                           "boundary: this source "
                           "position has no editable formatted counterpart.")));
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

void ContinuousEditor::setPaged(bool paged) {
  paged_ = paged;
  editor_->setPaged(paged);
}

void ContinuousEditor::setPageSetup(const PageSetup& setup) {
  pageSetup_ = setup;
  editor_->setPageSetup(setup);
}

int ContinuousEditor::pageCount() const {
  return editor_ ? editor_->pageCount() : 0;
}

int ContinuousEditor::currentPage() const {
  return editor_ ? editor_->currentPage() : 0;
}

QString ContinuousEditor::layoutWarning() const {
  return editor_ ? editor_->layoutWarning() : QString{};
}

}  // namespace markpad
