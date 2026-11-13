#include "page_canvas.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextLine>
#include <algorithm>
#include <cmath>

namespace markpad {
namespace {

constexpr double kMmToPoints = 72.0 / 25.4;

qreal mm(double value, qreal scale) {
  return value * kMmToPoints * scale;
}

}  // namespace

PageCanvas::PageCanvas(QWidget* parent) : QAbstractScrollArea(parent) {
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_InputMethodEnabled, true);
  setAccessibleName(QStringLiteral("Paginated document editor"));
  viewport()->setCursor(Qt::IBeamCursor);
  const auto* screen = QGuiApplication::primaryScreen();
  scale_ = screen && screen->logicalDotsPerInch() > 0.0
               ? screen->logicalDotsPerInch() / 72.0
               : 96.0 / 72.0;
  connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
    viewport()->update();
    updateCurrentPage();
  });
  refreshLayout();
}

void PageCanvas::setDocument(QTextDocument* document) {
  if (document_ == document) {
    return;
  }
  if (document_) {
    disconnect(document_, nullptr, this, nullptr);
  }
  document_ = document;
  if (document_) {
    connect(document_, &QTextDocument::contentsChanged, this, [this] {
      if (isVisible()) {
        refreshLayout();
        updateCurrentPage();
        viewport()->update();
      }
    });
    cursor_ = QTextCursor(document_);
  }
  refreshLayout();
  viewport()->update();
}

void PageCanvas::setPageSetup(const PageSetup& setup) {
  setup_ = setup;
  refreshLayout();
  viewport()->update();
}

void PageCanvas::setViewScale(qreal scale) {
  if (scale <= 0.0) {
    return;
  }
  scale_ = scale;
  refreshLayout();
  viewport()->update();
}

void PageCanvas::setLayoutSuspended(bool suspended) {
  layoutSuspended_ = suspended;
}

void PageCanvas::setShowSpecialCharacters(bool show) {
  if (showSpecialCharacters_ == show) {
    return;
  }
  showSpecialCharacters_ = show;
  viewport()->update();
}

void PageCanvas::setBreakPositions(const QSet<int>& positions) {
  breakPositions_ = positions;
  viewport()->update();
}

void PageCanvas::paintSpecialCharacters(QPainter& painter) {
  if (!document_ || pages_.empty()) {
    return;
  }
  QFont specialFont = font();
  specialFont.setPointSize(std::max(7, specialFont.pointSize() - 2));
  painter.setFont(specialFont);
  QColor color = palette().color(QPalette::PlaceholderText);
  color.setAlpha(190);
  painter.setPen(color);
  for (int page = 0; page < pageCount(); ++page) {
    if (!pageRect(page).intersects(viewport()->rect())) {
      continue;
    }
    const auto content = contentRect(page);
    const auto documentTop = pages_[size_t(page)].documentTop;
    for (const auto& unit : plan_.units) {
      if (unit.page != page) {
        continue;
      }
      for (int blockNumber = unit.firstBlock;
           blockNumber <= unit.lastBlock; ++blockNumber) {
        const auto block = document_->findBlockByNumber(blockNumber);
        if (!block.isValid() || !block.layout()) {
          continue;
        }
        const auto blockRect =
            document_->documentLayout()->blockBoundingRect(block);
        const auto text = block.text();
        const auto drawMark = [&](int offset, const QString& mark) {
          const auto line = block.layout()->lineForTextPosition(offset);
          if (!line.isValid()) {
            return;
          }
          const auto startX = line.cursorToX(offset);
          const auto endX = line.cursorToX(offset + 1);
          const QPointF documentPoint(
              blockRect.left() + line.x() + startX,
              blockRect.top() + line.y());
          const auto viewportPoint =
              content.topLeft() - QPointF(0, documentTop) + documentPoint;
          if (viewportPoint.y() < -line.height() ||
              viewportPoint.y() > viewport()->height()) {
            return;
          }
          if (mark == QStringLiteral("\u00b7")) {
            painter.drawEllipse(
                QPointF(viewportPoint.x() + (endX - startX) / 2.0,
                        viewportPoint.y() + line.height() / 2.0),
                1.3, 1.3);
          } else {
            painter.drawText(viewportPoint +
                                 QPointF(0, line.height() - 2),
                             mark);
          }
        };
        for (int offset = 0; offset < text.size(); ++offset) {
          const auto character = text.at(offset);
          if (character == QChar('\t')) {
            drawMark(offset, QStringLiteral("\u2192"));
          } else if (character == QChar(QChar::LineSeparator)) {
            drawMark(offset, QStringLiteral("\u21b5"));
          } else if (breakPositions_.contains(block.position() + offset)) {
            drawMark(offset, QStringLiteral("\u21b5"));
          } else if (character == QChar(' ') ||
                     character == QChar(QChar::Nbsp)) {
            drawMark(offset, QStringLiteral("\u00b7"));
          }
        }
        drawMark(block.length() - 1, QStringLiteral("\u00b6"));
      }
    }
  }
}

void PageCanvas::refreshLayout() {
  if (layoutSuspended_ || !isVisible()) {
    return;
  }
  pages_.clear();
  plan_ = {};
  if (!document_) {
    updateScrollRange();
    emit layoutChanged();
    emit layoutWarningChanged(QString{});
    return;
  }

  const auto pageSize = PageLayoutEngine::pageSize(setup_, scale_);
  const auto left = mm(setup_.marginLeftMm, scale_);
  const auto right = mm(setup_.marginRightMm, scale_);
  const auto top = mm(setup_.marginTopMm, scale_);
  const auto bottom = mm(setup_.marginBottomMm, scale_);
  const auto contentWidth = pageSize.width() - left - right;
  {
    const QSignalBlocker blocker(document_);
    document_->setDocumentMargin(0);
    if (contentWidth > 0.0) {
      document_->setTextWidth(contentWidth);
    }
  }
  plan_ = PagePlanner::plan(*document_, setup_, scale_);
  const auto maximumPosition = std::max(0, document_->characterCount() - 1);
  if (cursor_.position() > maximumPosition) {
    cursor_.setPosition(maximumPosition);
  }
  if (cursor_.anchor() > maximumPosition) {
    cursor_.setPosition(maximumPosition);
  }

  qreal y = 0.0;
  for (size_t index = 0; index < plan_.pages.size(); ++index) {
    qreal used = 0.0;
    for (const auto& unit : plan_.units) {
      if (unit.page == int(index)) {
        used = std::max(used, unit.documentTop - plan_.pages[index].documentTop +
                                  unit.height);
      }
    }
    const auto height =
        std::max(pageSize.height(), top + used + bottom);
    Sheet sheet;
    sheet.top = y;
    sheet.height = height;
    sheet.documentTop = plan_.pages[index].documentTop;
    pages_.push_back(sheet);
    y += height + pageGap_;
  }
  updateScrollRange();
  ensureCursorVisible();
  emit layoutChanged();
  emit layoutWarningChanged(plan_.warning);
  updateCurrentPage();
}

void PageCanvas::updateScrollRange() {
  qreal total = 0.0;
  if (!pages_.empty()) {
    total = pages_.back().top + pages_.back().height;
  }
  const auto maximum = int(std::max(0.0, std::ceil(total - viewport()->height())));
  verticalScrollBar()->setRange(0, maximum);
  verticalScrollBar()->setPageStep(viewport()->height());
  verticalScrollBar()->setSingleStep(24);
}

void PageCanvas::setTextCursor(const QTextCursor& cursor) {
  cursor_ = cursor;
  ensureCursorVisible();
  updateCurrentPage();
  viewport()->update();
  emit cursorPositionChanged();
}

QRectF PageCanvas::pageRect(int page) const {
  if (page < 0 || page >= int(pages_.size())) {
    return {};
  }
  const auto pageSize = PageLayoutEngine::pageSize(setup_, scale_);
  const auto available = viewport()->width();
  const auto x = std::max(0.0, (available - pageSize.width()) / 2.0);
  const auto y = pages_[size_t(page)].top - verticalScrollBar()->value();
  return QRectF(x, y, pageSize.width(), pages_[size_t(page)].height);
}

QRectF PageCanvas::contentRect(int page) const {
  if (page < 0 || page >= int(pages_.size())) {
    return {};
  }
  return pageRect(page).adjusted(mm(setup_.marginLeftMm, scale_),
                                 mm(setup_.marginTopMm, scale_),
                                 -mm(setup_.marginRightMm, scale_),
                                 -mm(setup_.marginBottomMm, scale_));
}

int PageCanvas::pageAt(const QPointF& viewportPoint) const {
  for (int index = 0; index < pageCount(); ++index) {
    if (pageRect(index).contains(viewportPoint)) {
      return index;
    }
  }
  return -1;
}

QPointF PageCanvas::documentPoint(const QPointF& viewportPoint, int page) const {
  const auto content = contentRect(page);
  return viewportPoint - content.topLeft() +
         QPointF(0, pages_[size_t(page)].documentTop);
}

QRectF PageCanvas::viewportRectForDocumentRect(const QRectF& rect,
                                               int page) const {
  if (page < 0 || page >= pageCount()) {
    return {};
  }
  const auto content = contentRect(page);
  return rect.translated(content.topLeft() -
                         QPointF(0, pages_[size_t(page)].documentTop));
}

int PageCanvas::documentPosition(const QPointF& viewportPoint) const {
  const auto page = pageAt(viewportPoint);
  if (page < 0 || !document_) {
    return -1;
  }
  const auto point = documentPoint(viewportPoint, page);
  return document_->documentLayout()->hitTest(point, Qt::FuzzyHit);
}

int PageCanvas::pageForBlock(int block) const {
  for (const auto& unit : plan_.units) {
    if (block >= unit.firstBlock && block <= unit.lastBlock) {
      return unit.page;
    }
  }
  return plan_.pages.empty() ? 0 : int(plan_.pages.size()) - 1;
}

int PageCanvas::currentPage() const {
  if (pages_.empty() || !document_) {
    return 0;
  }
  return pageForBlock(cursor_.block().blockNumber());
}

void PageCanvas::updateCurrentPage() {
  const auto page = currentPage();
  if (page != lastPage_) {
    lastPage_ = page;
    emit currentPageChanged(page);
  }
}

void PageCanvas::paintEvent(QPaintEvent* event) {
  QPainter painter(viewport());
  const auto desk = palette().color(QPalette::Window);
  const auto base = palette().color(QPalette::Base);
  const auto border = palette().color(QPalette::Mid);
  const auto text = palette().color(QPalette::Text);
  painter.fillRect(viewport()->rect(), desk);
  if (!document_ || pages_.empty()) {
    QAbstractScrollArea::paintEvent(event);
    return;
  }
  QFont footer = font();
  footer.setPointSize(std::max(7, font().pointSize() - 3));
  painter.setFont(footer);
  for (int index = 0; index < pageCount(); ++index) {
    const auto sheet = pageRect(index);
    if (!sheet.intersects(viewport()->rect())) {
      continue;
    }
    painter.fillRect(sheet, base);
    painter.setPen(QPen(border, 1.0));
    painter.drawRect(sheet);
    const auto content = contentRect(index);
    if (content.width() > 0.0 && content.height() > 0.0) {
      painter.save();
      painter.setClipRect(content);
      painter.translate(content.topLeft() -
                        QPointF(0, pages_[size_t(index)].documentTop));
      QAbstractTextDocumentLayout::PaintContext context;
      context.palette = palette();
      context.cursorPosition = cursor_.position();
      context.clip = QRectF(0, pages_[size_t(index)].documentTop,
                            content.width(), content.height());
      if (cursor_.hasSelection()) {
        QAbstractTextDocumentLayout::Selection selection;
        selection.cursor = cursor_;
        selection.format.setBackground(
            palette().color(QPalette::Highlight));
        selection.format.setForeground(
            palette().color(QPalette::HighlightedText));
        context.selections.append(selection);
      }
      document_->documentLayout()->draw(&painter, context);
      painter.restore();
    }
    if (mm(setup_.marginBottomMm, scale_) > 8.0) {
      painter.setPen(QPen(border));
      painter.drawText(
          QRectF(sheet.left(), sheet.bottom() - mm(setup_.marginBottomMm, scale_),
                 sheet.width(), mm(setup_.marginBottomMm, scale_)),
          Qt::AlignHCenter | Qt::AlignVCenter, QString::number(index + 1));
    }
  }

  if (!preedit_.isEmpty()) {
    const auto block = cursor_.block();
    const auto* layout = block.layout();
    const auto position = cursor_.position() - block.position();
    const auto line = layout ? layout->lineForTextPosition(position)
                             : QTextLine{};
    if (line.isValid()) {
      const auto blockRect =
          document_->documentLayout()->blockBoundingRect(block);
      const QPointF documentPoint(
          blockRect.left() + line.x() + line.cursorToX(position),
          blockRect.top() + line.y());
      const auto page = pageForBlock(block.blockNumber());
      const auto viewportPoint = viewportRectForDocumentRect(
          QRectF(documentPoint, QSizeF(1, line.height())), page);
      if (viewportPoint.top() + viewportPoint.height() >= 0 &&
          viewportPoint.top() <= viewport()->height()) {
        painter.setPen(QPen(text));
        painter.drawText(viewportPoint.topLeft(), preedit_);
        painter.drawLine(viewportPoint.bottomLeft(),
                         viewportPoint.bottomRight());
      }
    }
  }
  if (showSpecialCharacters_) {
    paintSpecialCharacters(painter);
  }
}

void PageCanvas::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    QAbstractScrollArea::mousePressEvent(event);
    return;
  }
  const auto position = documentPosition(event->position());
  if (position >= 0) {
    cursor_.setPosition(position);
    dragging_ = true;
    setFocus(Qt::MouseFocusReason);
    updateCurrentPage();
    viewport()->update();
    emit cursorPositionChanged();
    event->accept();
  }
}

void PageCanvas::mouseMoveEvent(QMouseEvent* event) {
  if (!dragging_) {
    QAbstractScrollArea::mouseMoveEvent(event);
    return;
  }
  const auto position = documentPosition(event->position());
  if (position >= 0) {
    cursor_.setPosition(position, QTextCursor::KeepAnchor);
    updateCurrentPage();
    viewport()->update();
    emit cursorPositionChanged();
    event->accept();
  }
}

void PageCanvas::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    dragging_ = false;
  }
  event->accept();
  QAbstractScrollArea::mouseReleaseEvent(event);
}

void PageCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
  const auto position = documentPosition(event->position());
  if (position >= 0) {
    cursor_.setPosition(position);
    cursor_.select(QTextCursor::WordUnderCursor);
    updateCurrentPage();
    viewport()->update();
    emit cursorPositionChanged();
  }
}

void PageCanvas::copySelection(bool cut) {
  if (!cursor_.hasSelection()) {
    return;
  }
  auto text = cursor_.selectedText();
  text.replace(QChar::ParagraphSeparator, QChar('\n'));
  QApplication::clipboard()->setText(text);
  if (cut) {
    cursor_.removeSelectedText();
    updateCurrentPage();
    viewport()->update();
  }
}

void PageCanvas::pasteClipboard() {
  const auto text = QApplication::clipboard()->text();
  if (text.isEmpty()) {
    return;
  }
  cursor_.insertText(text);
  updateCurrentPage();
  viewport()->update();
}

void PageCanvas::keyPressEvent(QKeyEvent* event) {
  if (!document_) {
    return;
  }
  if (event->matches(QKeySequence::Undo)) {
    document_->undo();
  } else if (event->matches(QKeySequence::Redo)) {
    document_->redo();
  } else if (event->matches(QKeySequence::SelectAll)) {
    cursor_.select(QTextCursor::Document);
    updateCurrentPage();
  } else if (event->matches(QKeySequence::Copy)) {
    copySelection(false);
  } else if (event->matches(QKeySequence::Cut)) {
    copySelection(true);
  } else if (event->matches(QKeySequence::Paste)) {
    pasteClipboard();
  } else {
    const auto mode = event->modifiers() & Qt::ShiftModifier
                          ? QTextCursor::KeepAnchor
                          : QTextCursor::MoveAnchor;
    switch (event->key()) {
      case Qt::Key_Left:
        cursor_.movePosition(QTextCursor::Left, mode);
        break;
      case Qt::Key_Right:
        cursor_.movePosition(QTextCursor::Right, mode);
        break;
      case Qt::Key_Up:
        cursor_.movePosition(QTextCursor::Up, mode);
        break;
      case Qt::Key_Down:
        cursor_.movePosition(QTextCursor::Down, mode);
        break;
      case Qt::Key_Home:
        cursor_.movePosition(
            event->modifiers() & Qt::ControlModifier ? QTextCursor::Start
                                                     : QTextCursor::StartOfLine,
            mode);
        break;
      case Qt::Key_End:
        cursor_.movePosition(
            event->modifiers() & Qt::ControlModifier ? QTextCursor::End
                                                     : QTextCursor::EndOfLine,
            mode);
        break;
      case Qt::Key_PageUp:
        verticalScrollBar()->triggerAction(QAbstractSlider::SliderPageStepSub);
        break;
      case Qt::Key_PageDown:
        verticalScrollBar()->triggerAction(QAbstractSlider::SliderPageStepAdd);
        break;
      case Qt::Key_Backspace:
        cursor_.deletePreviousChar();
        break;
      case Qt::Key_Delete:
        cursor_.deleteChar();
        break;
      case Qt::Key_Return:
      case Qt::Key_Enter:
        cursor_.insertBlock();
        break;
      case Qt::Key_Tab:
        cursor_.insertText(QStringLiteral("\t"));
        break;
      default:
        if (event->text().isEmpty() ||
            (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier |
                                   Qt::MetaModifier))) {
          QAbstractScrollArea::keyPressEvent(event);
          return;
        }
        cursor_.insertText(event->text());
        break;
    }
  }
  ensureCursorVisible();
  updateCurrentPage();
  viewport()->update();
  emit cursorPositionChanged();
}

void PageCanvas::inputMethodEvent(QInputMethodEvent* event) {
  if (!document_) {
    return;
  }
  if (!event->commitString().isEmpty()) {
    cursor_.insertText(event->commitString());
    preedit_.clear();
  } else {
    preedit_ = event->preeditString();
  }
  ensureCursorVisible();
  updateCurrentPage();
  viewport()->update();
  emit cursorPositionChanged();
  event->accept();
}

QVariant PageCanvas::inputMethodQuery(Qt::InputMethodQuery query) const {
  switch (query) {
    case Qt::ImEnabled:
      return true;
    case Qt::ImCursorRectangle: {
      if (!document_) {
        return QRectF{};
      }
      const auto block = cursor_.block();
      const auto* layout = block.layout();
      const auto position = cursor_.position() - block.position();
      const auto line =
          layout ? layout->lineForTextPosition(position) : QTextLine{};
      if (!line.isValid()) {
        return QRectF{};
      }
      const auto blockRect =
          document_->documentLayout()->blockBoundingRect(block);
      const auto local = QRectF(blockRect.left() + line.x() +
                                    line.cursorToX(position),
                                blockRect.top() + line.y(), 1, line.height());
      const auto page = pageForBlock(block.blockNumber());
      return viewportRectForDocumentRect(local, page).toRect();
    }
    case Qt::ImSurroundingText:
      return document_ ? document_->toPlainText() : QString{};
    case Qt::ImCursorPosition:
      return cursor_.position();
    case Qt::ImAnchorPosition:
      return cursor_.anchor();
    case Qt::ImCurrentSelection:
      return cursor_.selectedText();
    case Qt::ImHints:
      return int(Qt::ImhMultiLine | Qt::ImhNoPredictiveText);
    default:
      return QAbstractScrollArea::inputMethodQuery(query);
  }
}

void PageCanvas::ensureCursorVisible() {
  if (pages_.empty() || !document_) {
    return;
  }
  const auto page = currentPage();
  const auto cursorBlockRect =
      document_->documentLayout()->blockBoundingRect(cursor_.block());
  const auto cursorRect = viewportRectForDocumentRect(cursorBlockRect, page);
  auto value = verticalScrollBar()->value();
  if (cursorRect.top() < 0) {
    value += int(std::floor(cursorRect.top()));
  } else if (cursorRect.bottom() > viewport()->height()) {
    value += int(std::ceil(cursorRect.bottom() - viewport()->height()));
  }
  verticalScrollBar()->setValue(
      std::clamp(value, verticalScrollBar()->minimum(),
                 verticalScrollBar()->maximum()));
}

void PageCanvas::resizeEvent(QResizeEvent* event) {
  QAbstractScrollArea::resizeEvent(event);
  refreshLayout();
}

void PageCanvas::focusOutEvent(QFocusEvent* event) {
  preedit_.clear();
  viewport()->update();
  QAbstractScrollArea::focusOutEvent(event);
}

}  // namespace markpad
