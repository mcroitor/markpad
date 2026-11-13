#ifndef MARKPAD_EXPERIMENTS_PAGES_CANVAS_EDIT_H_
#define MARKPAD_EXPERIMENTS_PAGES_CANVAS_EDIT_H_

#include <QAbstractScrollArea>
#include <QRectF>
#include <QSizeF>
#include <QTextCursor>
#include <vector>

class QTextDocument;

namespace markpad::spike {

// Experimental single-document page canvas for Task 050. It deliberately
// supports only plain text, a fixed content margin and block-level page breaks;
// the production features of ContinuousEditor are not wired to it.
class CanvasEdit final : public QAbstractScrollArea {
 public:
  explicit CanvasEdit(QWidget* parent = nullptr);

  void setDocument(QTextDocument* document);
  QTextDocument* document() const { return document_; }
  QTextCursor textCursor() const { return cursor_; }
  void setTextCursor(const QTextCursor& cursor);

  void setPageSize(const QSizeF& size);
  QSizeF pageSize() const { return pageSize_; }
  void setPageMargin(qreal margin);
  int pageCount() const { return int(pages_.size()); }
  qreal documentTop(int page) const;
  QRectF pageRect(int page) const;
  QRectF contentRect(int page) const;
  void relayout();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void inputMethodEvent(QInputMethodEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  struct Page {
    qreal documentTop = 0.0;
  };
  int pageAt(const QPointF& viewportPoint) const;
  int documentPosition(const QPointF& viewportPoint) const;
  void updateScrollRange();

  QTextDocument* document_ = nullptr;
  QTextCursor cursor_;
  QSizeF pageSize_{200, 100};
  qreal margin_ = 8.0;
  std::vector<Page> pages_;
  bool dragging_ = false;
};

}  // namespace markpad::spike

#endif  // MARKPAD_EXPERIMENTS_PAGES_CANVAS_EDIT_H_
