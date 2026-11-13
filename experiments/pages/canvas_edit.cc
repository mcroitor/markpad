#include "canvas_edit.h"

#include <QAbstractTextDocumentLayout>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>
#include <algorithm>

namespace markpad::spike {
namespace {

constexpr qreal kPageGap = 24.0;

}  // namespace

CanvasEdit::CanvasEdit(QWidget* parent) : QAbstractScrollArea(parent) {
  setFocusPolicy(Qt::StrongFocus);
  viewport()->setCursor(Qt::IBeamCursor);
  connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
          [this] { viewport()->update(); });
}

void CanvasEdit::setDocument(QTextDocument* document) {
  if (document_ == document) {
    return;
  }
  if (document_) {
    disconnect(document_, nullptr, this, nullptr);
  }
  document_ = document;
  if (document_) {
    connect(document_, &QTextDocument::contentsChanged, this, [this] {
      relayout();
      viewport()->update();
    });
    cursor_ = QTextCursor(document_);
  }
  relayout();
  viewport()->update();
}

void CanvasEdit::setTextCursor(const QTextCursor& cursor) {
  cursor_ = cursor;
  viewport()->update();
}

void CanvasEdit::setPageSize(const QSizeF& size) {
  pageSize_ = size;
  relayout();
  viewport()->update();
}

void CanvasEdit::setPageMargin(qreal margin) {
  margin_ = std::max(0.0, margin);
  relayout();
  viewport()->update();
}

qreal contentWidth(const QSizeF& page, qreal margin) {
  return std::max(1.0, page.width() - 2.0 * margin);
}

qreal contentHeight(const QSizeF& page, qreal margin) {
  return std::max(1.0, page.height() - 2.0 * margin);
}

void CanvasEdit::relayout() {
  pages_.clear();
  if (!document_) {
    return;
  }
  document_->setDocumentMargin(0);
  const auto width = contentWidth(pageSize_, margin_);
  document_->setTextWidth(width);
  const auto capacity = contentHeight(pageSize_, margin_);
  pages_.push_back(Page{});
  qreal used = 0.0;
  for (auto block = document_->begin(); block.isValid(); block = block.next()) {
    const auto rect = document_->documentLayout()->blockBoundingRect(block);
    if (rect.height() <= 0.0) {
      continue;
    }
    if (used > 0.0 && used + rect.height() > capacity + 0.01) {
      pages_.push_back(Page{rect.top()});
      used = 0.0;
    }
    used += rect.height();
  }
  updateScrollRange();
}

void CanvasEdit::updateScrollRange() {
  const auto total = int(pages_.size() * (pageSize_.height() + kPageGap));
  const auto maxValue = std::max(0, total - viewport()->height());
  verticalScrollBar()->setRange(0, maxValue);
  verticalScrollBar()->setPageStep(viewport()->height());
  verticalScrollBar()->setSingleStep(24);
}

qreal CanvasEdit::documentTop(int page) const {
  if (page < 0 || page >= pageCount()) {
    return 0.0;
  }
  return pages_[size_t(page)].documentTop;
}

QRectF CanvasEdit::pageRect(int page) const {
  const auto top =
      page * (pageSize_.height() + kPageGap) - verticalScrollBar()->value();
  return QRectF(0, top, pageSize_.width(), pageSize_.height());
}

QRectF CanvasEdit::contentRect(int page) const {
  return pageRect(page).adjusted(margin_, margin_, -margin_, -margin_);
}

void CanvasEdit::paintEvent(QPaintEvent* event) {
  QPainter painter(viewport());
  const auto background = palette().color(QPalette::Window);
  const auto base = palette().color(QPalette::Base);
  const auto border = palette().color(QPalette::Mid);
  painter.fillRect(viewport()->rect(), background);
  if (!document_ || pages_.empty()) {
    QAbstractScrollArea::paintEvent(event);
    return;
  }
  for (int index = 0; index < pageCount(); ++index) {
    const auto sheet = pageRect(index);
    if (!sheet.intersects(viewport()->rect())) {
      continue;
    }
    painter.fillRect(sheet, base);
    painter.setPen(QPen(border, 1.0));
    painter.drawRect(sheet);
    const auto content = contentRect(index);
    painter.save();
    painter.setClipRect(content);
    painter.translate(content.topLeft() - QPointF(0, pages_[index].documentTop));
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette = palette();
    context.cursorPosition = cursor_.position();
    context.clip = QRectF(0, pages_[index].documentTop,
                          content.width(), content.height());
    if (cursor_.hasSelection()) {
      QAbstractTextDocumentLayout::Selection selection;
      selection.cursor = cursor_;
      selection.format.setBackground(palette().color(QPalette::Highlight));
      selection.format.setForeground(palette().color(QPalette::HighlightedText));
      context.selections.append(selection);
    }
    document_->documentLayout()->draw(&painter, context);
    painter.restore();
  }
}

int CanvasEdit::pageAt(const QPointF& viewportPoint) const {
  for (int index = 0; index < pageCount(); ++index) {
    if (pageRect(index).contains(viewportPoint)) {
      return index;
    }
  }
  return -1;
}

int CanvasEdit::documentPosition(const QPointF& viewportPoint) const {
  const auto page = pageAt(viewportPoint);
  if (page < 0 || !document_) {
    return -1;
  }
  const auto content = contentRect(page);
  const auto point = viewportPoint - content.topLeft() +
                     QPointF(0, pages_[page].documentTop);
  return document_->documentLayout()->hitTest(point, Qt::FuzzyHit);
}

void CanvasEdit::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    return;
  }
  const auto position = documentPosition(event->position());
  if (position >= 0) {
    cursor_.setPosition(position);
    dragging_ = true;
    setFocus(Qt::MouseFocusReason);
    viewport()->update();
  }
}

void CanvasEdit::mouseMoveEvent(QMouseEvent* event) {
  if (!dragging_) {
    return;
  }
  const auto position = documentPosition(event->position());
  if (position >= 0) {
    cursor_.setPosition(position, QTextCursor::KeepAnchor);
    viewport()->update();
  }
}

void CanvasEdit::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    dragging_ = false;
  }
}

void CanvasEdit::keyPressEvent(QKeyEvent* event) {
  if (!document_) {
    return;
  }
  if (event->matches(QKeySequence::Undo)) {
    document_->undo();
    return;
  }
  if (event->matches(QKeySequence::Redo)) {
    document_->redo();
    return;
  }
  switch (event->key()) {
    case Qt::Key_Left:
      cursor_.movePosition(QTextCursor::Left,
                           event->modifiers() & Qt::ShiftModifier
                               ? QTextCursor::KeepAnchor
                               : QTextCursor::MoveAnchor);
      break;
    case Qt::Key_Right:
      cursor_.movePosition(QTextCursor::Right,
                           event->modifiers() & Qt::ShiftModifier
                               ? QTextCursor::KeepAnchor
                               : QTextCursor::MoveAnchor);
      break;
    case Qt::Key_Up:
      cursor_.movePosition(QTextCursor::Up,
                           event->modifiers() & Qt::ShiftModifier
                               ? QTextCursor::KeepAnchor
                               : QTextCursor::MoveAnchor);
      break;
    case Qt::Key_Down:
      cursor_.movePosition(QTextCursor::Down,
                           event->modifiers() & Qt::ShiftModifier
                               ? QTextCursor::KeepAnchor
                               : QTextCursor::MoveAnchor);
      break;
    case Qt::Key_Home:
      cursor_.movePosition(QTextCursor::StartOfLine,
                           event->modifiers() & Qt::ShiftModifier
                               ? QTextCursor::KeepAnchor
                               : QTextCursor::MoveAnchor);
      break;
    case Qt::Key_End:
      cursor_.movePosition(QTextCursor::EndOfLine,
                           event->modifiers() & Qt::ShiftModifier
                               ? QTextCursor::KeepAnchor
                               : QTextCursor::MoveAnchor);
      break;
    case Qt::Key_Backspace:
      cursor_.deletePreviousChar();
      break;
    case Qt::Key_Delete:
      cursor_.deleteChar();
      break;
    default:
      if (!event->text().isEmpty()) {
        cursor_.insertText(event->text());
      } else {
        QAbstractScrollArea::keyPressEvent(event);
        return;
      }
      break;
  }
  viewport()->update();
}

void CanvasEdit::inputMethodEvent(QInputMethodEvent* event) {
  if (!document_) {
    return;
  }
  if (!event->commitString().isEmpty()) {
    cursor_.insertText(event->commitString());
    viewport()->update();
  }
  event->accept();
}

void CanvasEdit::resizeEvent(QResizeEvent* event) {
  QAbstractScrollArea::resizeEvent(event);
  relayout();
}

}  // namespace markpad::spike
