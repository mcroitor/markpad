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

PagePlan PagePlanner::plan(const QTextDocument& document,
                           const PageSetup& givenSetup, qreal scale) {
  const auto setup = normalized(givenSetup);
  PagePlan result;
  result.setup = setup;
  result.scale = scale;
  result.pageSize = PageLayoutEngine::pageSize(setup, scale);
  const auto size = result.pageSize;
  const auto left = mm(setup.marginLeftMm, scale);
  const auto top = mm(setup.marginTopMm, scale);
  const auto right = mm(setup.marginRightMm, scale);
  const auto bottom = mm(setup.marginBottomMm, scale);
  result.contentRect =
      QRectF(left, top, size.width() - left - right, size.height() - top - bottom);
  if (result.contentRect.width() <= 0.0 || result.contentRect.height() <= 0.0) {
    PageSlice slice;
    slice.contentRect = result.contentRect;
    result.pages.push_back(slice);
    result.overflow = true;
    result.warning =
        QStringLiteral("Page margins leave no usable content area");
    return result;
  }

  auto* layout = document.documentLayout();
  if (!layout) {
    PageSlice slice;
    slice.contentRect = result.contentRect;
    result.pages.push_back(slice);
    return result;
  }

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

  struct UnitInfo {
    int firstBlock;
    int lastBlock;
    qreal documentTop;
    qreal height;
    qreal width;
  };
  std::vector<UnitInfo> units;
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
    const auto unitTop = blocks[index].rect.top();
    const auto unitBottom =
        blocks[end].rect.bottom() + blocks[end].bottomMargin;
    const auto height = std::max(0.0, unitBottom - unitTop);
    if (height > 0.0) {
      qreal width = 0.0;
      for (size_t block = index; block <= end; ++block) {
        width = std::max(width, blocks[block].rect.width());
      }
      units.push_back({blocks[index].block.blockNumber(),
                       blocks[end].block.blockNumber(), unitTop, height, width});
    }
    index = end + 1;
  }

  const auto capacity = result.contentRect.height();
  PageSlice page;
  page.documentTop = 0.0;
  page.contentRect = result.contentRect;
  result.pages.push_back(page);
  qreal used = 0.0;
  for (const auto& unit : units) {
    if (used > 0.0 && used + unit.height > capacity + 0.01) {
      PageSlice next;
      next.documentTop = unit.documentTop;
      next.contentRect = result.contentRect;
      result.pages.push_back(next);
      used = 0.0;
    }
    const bool taller = unit.height > capacity;
    const bool wider = unit.width > result.contentRect.width() + 0.5;
    const bool overflow = taller || wider;
    PageUnit placement;
    placement.firstBlock = unit.firstBlock;
    placement.lastBlock = unit.lastBlock;
    placement.page = int(result.pages.size()) - 1;
    placement.documentTop = unit.documentTop;
    placement.height = unit.height;
    placement.overflow = overflow;
    result.units.push_back(placement);
    auto& slice = result.pages.back();
    if (slice.firstBlock < 0) {
      slice.firstBlock = unit.firstBlock;
    }
    slice.lastBlock = unit.lastBlock;
    slice.overflow = slice.overflow || overflow;
    if (overflow) {
      result.overflow = true;
      if (result.warning.isEmpty()) {
        result.warning =
            taller ? QStringLiteral(
                         "A block is taller than the usable page height and "
                         "overflows the page")
                   : QStringLiteral(
                         "A block is wider than the usable page width");
      }
    }
    used += unit.height;
  }
  return result;
}

PageLayoutResult PageLayoutEngine::layout(const QTextDocument& document,
                                          const PageSetup& setup,
                                          qreal scale) {
  const auto plan = PagePlanner::plan(document, setup, scale);
  PageLayoutResult result;
  for (const auto& page : plan.pages) {
    result.pages.push_back(PageGeometry{plan.pageSize, page.contentRect});
  }
  for (const auto& unit : plan.units) {
    BlockPlacement placement;
    placement.block = unit.firstBlock;
    placement.endBlock = unit.lastBlock;
    placement.page = unit.page;
    placement.overflow = unit.overflow;
    placement.rect = QRectF(
        0.0, unit.documentTop - plan.pages[size_t(unit.page)].documentTop,
        plan.contentRect.width(), unit.height);
    result.placements.push_back(placement);
  }
  result.overflow = plan.overflow;
  result.warning = plan.warning;
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
