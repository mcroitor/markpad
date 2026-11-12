#include "page_text_edit.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
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

void PageTextEdit::setPaged(bool paged) {
  if (paged_ == paged) {
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

  const auto page = PageLayoutEngine::pageSize(setup_);
  const auto left = mm(setup_.marginLeftMm);
  const auto right = mm(setup_.marginRightMm);
  const auto content = page.width() - left - right;
  document()->setDocumentMargin(left);
  if (content > 0.0) {
    document()->setTextWidth(content);
  }
  // Center the page in the available widget area. The viewport margins also
  // keep the QTextEdit scrollbar outside the page sheet.
  const auto pageX = std::max(0.0, (width() - page.width()) / 2.0);
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
  layout_ = PageLayoutEngine::layout(*document(), setup_);
  if (layout_.pages.empty()) {
    relayouting_ = false;
    viewport()->update();
    return;
  }

  const auto page = PageLayoutEngine::pageSize(setup_);
  const auto top = mm(setup_.marginTopMm);
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
        qreal(pageIndex) * (page.height() + kPageGap) + top;
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

void PageTextEdit::paintEvent(QPaintEvent* event) {
  QTextEdit::paintEvent(event);
  if (!paged_) {
    return;
  }
  const auto page = PageLayoutEngine::pageSize(setup_);
  const auto pageHeight = page.height();
  const auto pageWidth = page.width();
  const auto bottom = mm(setup_.marginBottomMm);
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
        qreal(pageIndex) * (pageHeight + kPageGap) - scroll;
    const QRectF sheetRect(0.0, top, pageWidth, pageHeight);
    if (pageIndex + 1 < pages) {
      painter.fillRect(QRectF(0.0, top + pageHeight, viewport()->width(),
                              kPageGap),
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
  if (layout_.overflow) {
    painter.setPen(QColor(170, 80, 20));
    painter.drawText(QRectF(4.0, 4.0, viewport()->width() - 8.0,
                            metrics.height() + 4.0),
                     Qt::AlignLeft | Qt::AlignTop,
                     QStringLiteral("Page overflow"));
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
