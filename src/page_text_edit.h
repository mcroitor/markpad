#ifndef MARKPAD_SRC_PAGE_TEXT_EDIT_H_
#define MARKPAD_SRC_PAGE_TEXT_EDIT_H_

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

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  void updatePageWidth();

  PageSetup setup_;
  bool paged_ = false;
};

}  // namespace markpad

#endif  // MARKPAD_SRC_PAGE_TEXT_EDIT_H_
