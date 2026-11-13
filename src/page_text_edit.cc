#include "page_text_edit.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <QTextTableFormat>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace markpad {
namespace {

constexpr double kMmToPoints = 72.0 / 25.4;
constexpr qreal kPageGap = 24.0;

qreal mm(double value) {
  return value * kMmToPoints;
}

qreal px(double value, qreal scale) {
  return mm(value) * scale;
}

}  // namespace

PageTextEdit::PageTextEdit(QWidget* parent) : QTextEdit(parent) {
  setAcceptRichText(false);
  normalMargin_ = document()->documentMargin();
  connect(document(), &QTextDocument::contentsChanged, this,
          [this] { viewport()->update(); });
  connect(this, &QTextEdit::textChanged, this, [this] {
    if (paged_ && !relayouting_) {
      refreshPageLayout();
    }
  });
  updatePageWidth();
}

qreal PageTextEdit::viewScale() const {
  const auto* screen = QGuiApplication::primaryScreen();
  const auto dpi = screen ? screen->logicalDotsPerInch() : 96.0;
  return dpi > 0.0 ? dpi / 72.0 : 96.0 / 72.0;
}

void PageTextEdit::setNormalLineWrapMode(QTextEdit::LineWrapMode mode) {
  normalWrapMode_ = mode;
  if (!paged_) {
    setLineWrapMode(mode);
  }
}

void PageTextEdit::setShowSpecialCharacters(bool show) {
  if (showSpecialCharacters_ == show) {
    return;
  }
  showSpecialCharacters_ = show;
  viewport()->update();
}

void PageTextEdit::setBreakPositions(const QSet<int>& positions) {
  breakPositions_ = positions;
  viewport()->update();
}

void PageTextEdit::setPaged(bool paged) {
  if (paged_ == paged) {
    if (!paged) {
      // Another surface may have changed document width/margin (page canvas).
      updatePageWidth();
    }
    return;
  }
  paged_ = paged;
  if (!paged_) {
    layout_ = {};
    resetPageMargins();
  }
  updatePageWidth();
  if (paged_) {
    refreshPageLayout();
  } else {
    setLineWrapMode(normalWrapMode_);
  }
  viewport()->update();
}

void PageTextEdit::setPageSetup(const PageSetup& setup) {
  setup_ = setup;
  if (paged_) {
    refreshPageLayout();
  } else {
    updatePageWidth();
  }
  viewport()->update();
}

void PageTextEdit::updatePageWidth() {
  if (adjusting_) {
    return;
  }
  adjusting_ = true;
  if (!paged_) {
    document()->setDocumentMargin(normalMargin_);
    setViewportMargins(0, 0, 0, 0);
    const auto width = viewport()->width() - 2.0 * normalMargin_;
    if (width > 0.0) {
      document()->setTextWidth(width);
    }
    adjusting_ = false;
    return;
  }

  const auto scale = viewScale();
  const auto page = PageLayoutEngine::pageSize(setup_, scale);
  const auto left = px(setup_.marginLeftMm, scale);
  const auto right = px(setup_.marginRightMm, scale);
  const auto content = page.width() - left - right;
  const auto documentWidth = page.width() + left - right;
  document()->setDocumentMargin(left);
  if (content > 0.0) {
    setLineWrapMode(QTextEdit::FixedPixelWidth);
    setLineWrapColumnOrWidth(qRound(documentWidth));
    document()->setTextWidth(documentWidth);
  }
  // Center the page in the available widget area. Account for the vertical
  // scrollbar so the page's right border is not clipped.
  const auto scrollbarWidth =
      verticalScrollBar() ? verticalScrollBar()->sizeHint().width() : 0;
  const auto available = width() - scrollbarWidth;
  const auto pageX =
      std::max(0.0, (available - page.width()) / 2.0 - 2.0);
  setViewportMargins(qRound(pageX), 0, qRound(pageX), 0);
  adjusting_ = false;
}

void PageTextEdit::schedulePageLayout() {
  if (layoutScheduled_ || !paged_) {
    return;
  }
  layoutScheduled_ = true;
  QTimer::singleShot(0, this, [this] {
    layoutScheduled_ = false;
    if (paged_) {
      refreshPageLayout();
    }
  });
}

void PageTextEdit::resetPageMargins() {
  if (!document()) {
    return;
  }
  const QSignalBlocker blocker(document());
  const bool modified = document()->isModified();
  QSet<QTextTable*> tables;
  for (auto block = document()->begin(); block.isValid(); block = block.next()) {
    auto format = block.blockFormat();
    if (format.topMargin() != 0.0) {
      format.setTopMargin(0.0);
      QTextCursor cursor(block);
      cursor.setBlockFormat(format);
    }
    QTextCursor cursor(block);
    auto* table = cursor.currentTable();
    if (table && !tables.contains(table)) {
      tables.insert(table);
      auto tableFormat = table->format();
      if (tableFormat.topMargin() != 0.0) {
        tableFormat.setTopMargin(0.0);
        table->setFormat(tableFormat);
      }
    }
  }
  document()->setModified(modified);
}

void PageTextEdit::refreshPageLayout() {
  if (!paged_ || relayouting_ || !document()) {
    return;
  }
  relayouting_ = true;
  layoutScheduled_ = false;
  updatePageWidth();
  resetPageMargins();
  const auto scale = viewScale();
  layout_ = PageLayoutEngine::layout(*document(), setup_, scale);
  if (layout_.pages.empty()) {
    relayouting_ = false;
    viewport()->update();
    return;
  }

  const auto page = PageLayoutEngine::pageSize(setup_, scale);
  const auto top = px(setup_.marginTopMm, scale);
  const auto gap = kPageGap * scale;
  std::vector<int> firstBlocks(layout_.pages.size(), -1);
  for (const auto& placement : layout_.placements) {
    if (placement.page < 0 ||
        placement.page >= int(firstBlocks.size())) {
      continue;
    }
    if (firstBlocks[size_t(placement.page)] < 0) {
      firstBlocks[size_t(placement.page)] = placement.block;
    }
  }

  const QSignalBlocker blocker(document());
  const bool modified = document()->isModified();
  for (size_t pageIndex = 0; pageIndex < firstBlocks.size(); ++pageIndex) {
    if (firstBlocks[pageIndex] < 0) {
      continue;
    }
    auto block = document()->findBlockByNumber(firstBlocks[pageIndex]);
    if (!block.isValid()) {
      continue;
    }
    const auto current =
        document()->documentLayout()->blockBoundingRect(block).top();
    const auto desired =
        qreal(pageIndex) * (page.height() + gap) + top;
    const auto delta = desired - current;
    if (delta <= 0.5) {
      continue;
    }
    QTextCursor cursor(block);
    if (auto* table = cursor.currentTable()) {
      auto format = table->format();
      format.setTopMargin(format.topMargin() + delta);
      table->setFormat(format);
    } else {
      auto format = block.blockFormat();
      format.setTopMargin(format.topMargin() + delta);
      cursor.setBlockFormat(format);
    }
  }
  document()->setModified(modified);
  relayouting_ = false;
  viewport()->update();
}

int PageTextEdit::pageCount() const {
  if (!paged_) {
    return 0;
  }
  return layout_.pages.empty() ? 1 : int(layout_.pages.size());
}

int PageTextEdit::currentPage() const {
  if (!paged_) {
    return 0;
  }
  const auto block = document()->findBlock(textCursor().position()).blockNumber();
  for (const auto& placement : layout_.placements) {
    if (block >= placement.block && block <= placement.endBlock) {
      return placement.page;
    }
  }
  return layout_.pages.empty() ? 0 : int(layout_.pages.size()) - 1;
}

QString PageTextEdit::layoutWarning() const {
  return paged_ ? layout_.warning : QString{};
}

void PageTextEdit::paintSpecialCharacters(QPainter& painter) {
  if (!document()) {
    return;
  }
  const auto first = cursorForPosition(QPoint(0, 0)).block();
  const auto last = cursorForPosition(
                        QPoint(viewport()->width() - 1,
                               viewport()->height() - 1))
                        .block();
  if (!first.isValid() || !last.isValid()) {
    return;
  }
  QFont specialFont = font();
  specialFont.setPointSize(std::max(7, specialFont.pointSize() - 2));
  painter.setFont(specialFont);
  QColor color = palette().color(QPalette::PlaceholderText);
  color.setAlpha(190);
  painter.setPen(color);
  int visited = 0;
  for (auto block = first; block.isValid() && visited < 2000;
       block = block.next(), ++visited) {
    const auto text = block.text();
    for (int offset = 0; offset < text.size(); ++offset) {
      const auto character = text.at(offset);
      if (character != QChar(' ') && character != QChar('\t') &&
          character != QChar(QChar::Nbsp) &&
          character != QChar(QChar::LineSeparator)) {
        continue;
      }
      QTextCursor position(document());
      position.setPosition(block.position() + offset);
      const auto rect = cursorRect(position);
      if (rect.isEmpty() || rect.bottom() < 0 ||
          rect.top() > viewport()->height()) {
        continue;
      }
      if (character == QChar('\t')) {
        painter.drawText(rect.topLeft() + QPoint(0, rect.height() - 3),
                         QStringLiteral("\u2192"));
      } else if (character == QChar(QChar::LineSeparator)) {
        painter.drawText(rect.topLeft() + QPoint(0, rect.height() - 3),
                         QStringLiteral("\u21b5"));
      } else if (breakPositions_.contains(block.position() + offset)) {
        painter.drawText(rect.topLeft() + QPoint(0, rect.height() - 3),
                         QStringLiteral("\u21b5"));
      } else {
        QTextCursor next(document());
        next.setPosition(block.position() + offset + 1);
        const auto nextRect = cursorRect(next);
        const auto width = std::max(1, nextRect.left() - rect.left());
        painter.drawEllipse(
            QPointF(rect.left() + width / 2.0, rect.center().y()), 1.3, 1.3);
      }
    }
    QTextCursor end(document());
    end.setPosition(block.position() + block.length() - 1);
    const auto rect = cursorRect(end);
    if (!rect.isEmpty() && rect.left() < viewport()->width() &&
        rect.bottom() > 0 && rect.top() < viewport()->height()) {
      painter.drawText(rect.topLeft() + QPoint(1, rect.height() - 2),
                       QStringLiteral("\u00b6"));
    }
    if (block == last) {
      break;
    }
  }
}

void PageTextEdit::paintEvent(QPaintEvent* event) {
  QTextEdit::paintEvent(event);
  if (!paged_) {
    if (showSpecialCharacters_) {
      QPainter painter(viewport());
      paintSpecialCharacters(painter);
    }
    return;
  }
  const auto scale = viewScale();
  const auto page = PageLayoutEngine::pageSize(setup_, scale);
  const auto pageHeight = page.height();
  const auto pageWidth = page.width();
  const auto bottom = px(setup_.marginBottomMm, scale);
  const auto gap = kPageGap * scale;
  const auto pages = std::max(1, int(layout_.pages.size()));
  const auto scroll =
      qreal(verticalScrollBar() ? verticalScrollBar()->value() : 0);

  QPainter painter(viewport());
  painter.setRenderHint(QPainter::Antialiasing, false);
  const auto gapColor = palette().color(QPalette::Window);
  const auto borderColor = palette().color(QPalette::Mid);
  QFont footerFont = font();
  footerFont.setPointSize(8);
  painter.setFont(footerFont);
  const QFontMetrics metrics(footerFont);

  for (int pageIndex = 0; pageIndex < pages; ++pageIndex) {
    const auto top =
        qreal(pageIndex) * (pageHeight + gap) - scroll;
    const QRectF sheetRect(0.0, top, pageWidth, pageHeight);
    if (pageIndex + 1 < pages) {
      painter.fillRect(QRectF(0.0, top + pageHeight, viewport()->width(),
                              gap),
                       gapColor);
    }
    if (pageWidth < viewport()->width()) {
      painter.fillRect(
          QRectF(pageWidth, top, viewport()->width() - pageWidth, pageHeight),
          gapColor);
    }
    painter.setPen(QPen(borderColor, 1.0));
    painter.drawRect(sheetRect);
    if (bottom > 0.0) {
      const QRectF footer(0.0, top + pageHeight - bottom, pageWidth, bottom);
      painter.setPen(borderColor);
      painter.drawText(footer, Qt::AlignHCenter | Qt::AlignVCenter,
                       QString::number(pageIndex + 1));
    }
  }
  const auto lastBottom =
      qreal(pages - 1) * (pageHeight + gap) + pageHeight - scroll;
  if (lastBottom < viewport()->height()) {
    painter.fillRect(
        QRectF(0.0, lastBottom, viewport()->width(),
               viewport()->height() - lastBottom),
        gapColor);
  }
  if (layout_.overflow) {
    painter.setPen(QColor(170, 80, 20));
    painter.drawText(QRectF(4.0, 4.0, viewport()->width() - 8.0,
                            metrics.height() + 4.0),
                     Qt::AlignLeft | Qt::AlignTop,
                     QStringLiteral("Page overflow"));
  }
  if (showSpecialCharacters_) {
    paintSpecialCharacters(painter);
  }
}

void PageTextEdit::resizeEvent(QResizeEvent* event) {
  QTextEdit::resizeEvent(event);
  if (adjusting_) {
    return;
  }
  updatePageWidth();
  if (paged_) {
    schedulePageLayout();
  }
}

}  // namespace markpad
