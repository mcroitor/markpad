#include "page_text_edit.h"

#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QPaintEvent>
#include <QResizeEvent>
#include <algorithm>

namespace markpad {

PageTextEdit::PageTextEdit(QWidget* parent) : QTextEdit(parent) {
  setAcceptRichText(false);
  connect(document(), &QTextDocument::contentsChanged, this,
          [this] { viewport()->update(); });
}

void PageTextEdit::setPaged(bool paged) {
  paged_ = paged;
  updatePageWidth();
  viewport()->update();
}

void PageTextEdit::setPageSetup(const PageSetup& setup) {
  setup_ = setup;
  updatePageWidth();
  viewport()->update();
}

void PageTextEdit::updatePageWidth() {
  if (!paged_) {
    document()->setTextWidth(-1);
    return;
  }
  const auto page = PageLayoutEngine::pageSize(setup_);
  const auto width =
      page.width() - (setup_.marginLeftMm + setup_.marginRightMm) * 72.0 / 25.4;
  if (width > 0.0) {
    document()->setTextWidth(width);
  }
}

int PageTextEdit::pageCount() const {
  if (!paged_) {
    return 0;
  }
  return int(PageLayoutEngine::layout(*document(), setup_).pages.size());
}

int PageTextEdit::currentPage() const {
  if (!paged_) {
    return 0;
  }
  return PageLayoutEngine::pageForPosition(*document(), setup_,
                                           textCursor().position());
}

QString PageTextEdit::layoutWarning() const {
  if (!paged_) {
    return {};
  }
  return PageLayoutEngine::layout(*document(), setup_).warning;
}

void PageTextEdit::paintEvent(QPaintEvent* event) {
  QTextEdit::paintEvent(event);
  if (!paged_) {
    return;
  }
  const auto layout = PageLayoutEngine::layout(*document(), setup_);
  if (layout.pages.size() <= 1 && !layout.overflow) {
    return;
  }
  QPainter painter(viewport());
  painter.setRenderHint(QPainter::Antialiasing, false);
  QPen pen(QColor(120, 120, 120));
  pen.setStyle(Qt::DashLine);
  painter.setPen(pen);
  for (size_t page = 1; page < layout.pages.size(); ++page) {
    const auto first = std::find_if(
        layout.placements.begin(), layout.placements.end(),
        [page](const BlockPlacement& placement) {
          return placement.page == int(page);
        });
    if (first == layout.placements.end()) {
      continue;
    }
    const auto cursor =
        QTextCursor(document()->findBlockByNumber(first->block));
    const auto rect = cursorRect(cursor);
    const auto y = rect.top() - 4;
    painter.drawLine(0, y, viewport()->width(), y);
    painter.drawText(4, std::max(12, y - 4),
                     QStringLiteral("Page %1").arg(page + 1));
  }
  if (layout.overflow) {
    painter.setPen(QColor(170, 80, 20));
    painter.drawText(4, 12, QStringLiteral("Page overflow"));
  }
}

void PageTextEdit::resizeEvent(QResizeEvent* event) {
  QTextEdit::resizeEvent(event);
  updatePageWidth();
}

}  // namespace markpad
