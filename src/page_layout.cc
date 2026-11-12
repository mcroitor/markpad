#include "page_layout.h"

#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <algorithm>
#include <cmath>
#include <vector>

namespace markpad {
namespace {

constexpr double kMmToPoints = 72.0 / 25.4;

double mm(double value, qreal scale = 1.0) {
  return value * kMmToPoints * scale;
}

PageSetup normalized(const PageSetup& setup) {
  PageSetup result = setup;
  const auto clampMargin = [](double value) {
    return value < 0.0 ? 0.0 : (value > 200.0 ? 200.0 : value);
  };
  result.marginTopMm = clampMargin(result.marginTopMm);
  result.marginRightMm = clampMargin(result.marginRightMm);
  result.marginBottomMm = clampMargin(result.marginBottomMm);
  result.marginLeftMm = clampMargin(result.marginLeftMm);
  return result;
}

}  // namespace

QSizeF PageLayoutEngine::pageSize(const PageSetup& setup, qreal scale) {
  QSizeF size;
  switch (setup.size) {
    case PageSize::kLetter:
      size = QSizeF(mm(215.9, scale), mm(279.4, scale));
      break;
    case PageSize::kLegal:
      size = QSizeF(mm(215.9, scale), mm(355.6, scale));
      break;
    case PageSize::kA4:
    default:
      size = QSizeF(mm(210.0, scale), mm(297.0, scale));
      break;
  }
  if (setup.orientation == PageOrientation::kLandscape) {
    size = QSizeF(size.height(), size.width());
  }
  return size;
}

PageLayoutResult PageLayoutEngine::layout(const QTextDocument& document,
                                          const PageSetup& setup,
                                          qreal scale) {
  const auto page = normalized(setup);
  const auto size = pageSize(page, scale);
  const auto content = QRectF(mm(page.marginLeftMm, scale),
                              mm(page.marginTopMm, scale),
                              size.width() - mm(page.marginLeftMm, scale) -
                                  mm(page.marginRightMm, scale),
                              size.height() - mm(page.marginTopMm, scale) -
                                  mm(page.marginBottomMm, scale));
  PageLayoutResult result;
  if (content.width() <= 0.0 || content.height() <= 0.0) {
    result.pages.push_back(PageGeometry{size, content});
    result.overflow = true;
    result.warning =
        QStringLiteral("Page margins leave no usable content area");
    return result;
  }

  auto* layout = document.documentLayout();
  if (!layout) {
    result.pages.push_back(PageGeometry{size, content});
    return result;
  }

  int pageIndex = 0;
  double cursorY = 0.0;
  const auto addPage = [&result, size, content]() {
    result.pages.push_back(PageGeometry{size, content});
  };
  addPage();

  struct BlockInfo {
    QTextBlock block;
    QRectF rect;
    int unit = 0;
    QTextTable* table = nullptr;
    qreal bottomMargin = 0.0;
  };
  std::vector<BlockInfo> blocks;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    const auto rect = layout->blockBoundingRect(block);
    if (rect.height() <= 0.0) {
      continue;
    }
    const auto format = block.blockFormat();
    const auto unit = format.hasProperty(kPageUnitProperty)
                          ? format.intProperty(kPageUnitProperty)
                          : 0;
    QTextTable* table = nullptr;
    {
      QTextCursor cursor(block);
      table = cursor.currentTable();
    }
    blocks.push_back({block, rect, unit, table, format.bottomMargin()});
  }

  for (size_t index = 0; index < blocks.size();) {
    size_t end = index;
    if (blocks[index].table) {
      while (end + 1 < blocks.size() &&
             blocks[end + 1].table == blocks[index].table) {
        ++end;
      }
    } else if (blocks[index].unit != 0) {
      while (end + 1 < blocks.size() &&
             blocks[end + 1].table == nullptr &&
             blocks[end + 1].unit == blocks[index].unit) {
        ++end;
      }
    }
    const auto top = blocks[index].rect.top();
    const auto bottom =
        blocks[end].rect.bottom() + blocks[end].bottomMargin;
    const auto height = std::max(0.0, bottom - top);
    if (height <= 0.0) {
      index = end + 1;
      continue;
    }
    const bool oversized = height > content.height();
    if (cursorY > 0.0 && cursorY + height > content.height() + 0.01) {
      ++pageIndex;
      addPage();
      cursorY = 0.0;
    }
    BlockPlacement placement;
    placement.block = blocks[index].block.blockNumber();
    placement.endBlock = blocks[end].block.blockNumber();
    placement.page = pageIndex;
    placement.overflow = oversized;
    placement.rect = QRectF(0.0, cursorY, content.width(), height);
    result.placements.push_back(placement);
    if (oversized) {
      result.overflow = true;
      if (result.warning.isEmpty()) {
        result.warning = QStringLiteral(
            "A block is taller than the usable page height and overflows "
            "the page");
      }
    }
    cursorY += height;
    index = end + 1;
  }
  return result;
}

int PageLayoutEngine::pageForPosition(const QTextDocument& document,
                                      const PageSetup& setup, int position,
                                      qreal scale) {
  const auto result = layout(document, setup, scale);
  const auto block = document.findBlock(position).blockNumber();
  for (const auto& placement : result.placements) {
    if (block >= placement.block && block <= placement.endBlock) {
      return placement.page;
    }
  }
  return result.pages.empty() ? 0 : int(result.pages.size()) - 1;
}

}  // namespace markpad
