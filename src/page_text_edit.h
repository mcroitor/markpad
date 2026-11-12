#ifndef MARKPAD_SRC_PAGE_TEXT_EDIT_H_
#define MARKPAD_SRC_PAGE_TEXT_EDIT_H_

#include <QMargins>
#include <QTextEdit>

#include "page_layout.h"
#include "settings.h"

namespace markpad {

class PageTextEdit : public QTextEdit {
 public:
  explicit PageTextEdit(QWidget* parent = nullptr);

  void setPaged(bool paged);
  bool paged() const { return paged_; }
  void setPageSetup(const PageSetup& setup);
  PageSetup pageSetup() const { return setup_; }
  int pageCount() const;
  int currentPage() const;
  QString layoutWarning() const;
  const PageLayoutResult& pageLayout() const { return layout_; }
  QMargins viewportPageMargins() const { return viewportMargins(); }
  qreal viewScale() const;
  void setNormalLineWrapMode(QTextEdit::LineWrapMode mode);
  void refreshPageLayout();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  void updatePageWidth();
  void schedulePageLayout();
  void resetPageMargins();

  PageSetup setup_;
  PageLayoutResult layout_;
  bool paged_ = false;
  bool relayouting_ = false;
  bool layoutScheduled_ = false;
  bool adjusting_ = false;
  qreal normalMargin_ = 4.0;
  QTextEdit::LineWrapMode normalWrapMode_ = QTextEdit::WidgetWidth;
};

}  // namespace markpad

#endif  // MARKPAD_SRC_PAGE_TEXT_EDIT_H_
