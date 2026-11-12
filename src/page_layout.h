#ifndef MARKPAD_SRC_PAGE_LAYOUT_H_
#define MARKPAD_SRC_PAGE_LAYOUT_H_

#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QTextFormat>
#include <vector>

#include "settings.h"

class QTextDocument;

namespace markpad {

// Blocks carrying the same non-zero page-unit property are placed together.
// ContinuousEditor uses this for fenced code blocks so they never split.
constexpr int kPageUnitProperty = QTextFormat::UserProperty + 700;

struct PageGeometry {
  QSizeF pageSize;
  QRectF contentRect;
};

struct BlockPlacement {
  int block = 0;
  int endBlock = 0;  // Last block for atomic units (tables, fenced code).
  QRectF rect;       // Position within the page content area.
  int page = 0;
  bool overflow = false;
};

struct PageLayoutResult {
  std::vector<PageGeometry> pages;
  std::vector<BlockPlacement> placements;
  bool overflow = false;
  QString warning;
};

// Deterministic, window-independent page layout. Blocks are placed as units;
// a block taller than the content area is allowed to overflow on its own page
// and produces an explicit warning.
class PageLayoutEngine final {
 public:
  static QSizeF pageSize(const PageSetup& setup);
  static PageLayoutResult layout(const QTextDocument& document,
                                 const PageSetup& setup);
  static int pageForPosition(const QTextDocument& document,
                             const PageSetup& setup, int position);
};

}  // namespace markpad

#endif  // MARKPAD_SRC_PAGE_LAYOUT_H_
