#ifndef MARKPAD_SRC_PAGE_CANVAS_H_
#define MARKPAD_SRC_PAGE_CANVAS_H_

#include <QAbstractScrollArea>
#include <QRectF>
#include <QSet>
#include <QTextCursor>
#include <QTextDocument>
#include <vector>

#include "page_layout.h"
#include "settings.h"

class QPainter;

namespace markpad {

// Editable paged surface backed by one QTextDocument. Pagination is view-only:
// the document text, formats and undo history are not changed by the page plan.
class PageCanvas final : public QAbstractScrollArea {
  Q_OBJECT

 public:
  explicit PageCanvas(QWidget* parent = nullptr);

  void setDocument(QTextDocument* document);  // Non-owning.
  QTextDocument* document() const { return document_; }

  void setPageSetup(const PageSetup& setup);
  PageSetup pageSetup() const { return setup_; }
  void setViewScale(qreal scale);
  qreal viewScale() const { return scale_; }

  void refreshLayout();
  void setLayoutSuspended(bool suspended);
  void setShowSpecialCharacters(bool show);
  bool showSpecialCharacters() const { return showSpecialCharacters_; }
  void setBreakPositions(const QSet<int>& positions);
  int pageCount() const { return int(pages_.size()); }
  int currentPage() const;
  QString layoutWarning() const { return plan_.warning; }
  bool hasOverflow() const { return plan_.overflow; }

  QTextCursor textCursor() const { return cursor_; }
  void setTextCursor(const QTextCursor& cursor);
  const PagePlan& pagePlan() const { return plan_; }

  QRectF pageRect(int page) const;
  QRectF contentRect(int page) const;

 signals:
  void layoutChanged();
  void currentPageChanged(int page);
  void layoutWarningChanged(const QString& warning);
  void cursorPositionChanged();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void inputMethodEvent(QInputMethodEvent* event) override;
  QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
  void resizeEvent(QResizeEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;

 private:
  struct Sheet {
    qreal top = 0.0;
    qreal height = 0.0;
    qreal documentTop = 0.0;
  };

  int pageAt(const QPointF& viewportPoint) const;
  int documentPosition(const QPointF& viewportPoint) const;
  QPointF documentPoint(const QPointF& viewportPoint, int page) const;
  QRectF viewportRectForDocumentRect(const QRectF& rect, int page) const;
  int pageForBlock(int block) const;
  void updateScrollRange();
  void ensureCursorVisible();
  void updateCurrentPage();
  void copySelection(bool cut);
  void pasteClipboard();
  void paintSpecialCharacters(QPainter& painter);

  QTextDocument* document_ = nullptr;
  QTextCursor cursor_;
  PageSetup setup_;
  PagePlan plan_;
  std::vector<Sheet> pages_;
  qreal scale_ = 1.0;
  qreal pageGap_ = 24.0;
  QString preedit_;
  bool dragging_ = false;
  bool layoutSuspended_ = false;
  bool showSpecialCharacters_ = false;
  QSet<int> breakPositions_;
  int lastPage_ = -1;
};

}  // namespace markpad

#endif  // MARKPAD_SRC_PAGE_CANVAS_H_
