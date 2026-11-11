#include "page_layout.h"

#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextDocument>
#include <cmath>

namespace markpad {
namespace {

constexpr double kMmToPoints = 72.0 / 25.4;

double mm(double value) {
  return value * kMmToPoints;
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

QSizeF PageLayoutEngine::pageSize(const PageSetup& setup) {
  QSizeF size;
  switch (setup.size) {
    case PageSize::kLetter:
      size = QSizeF(mm(215.9), mm(279.4));
      break;
    case PageSize::kLegal:
      size = QSizeF(mm(215.9), mm(355.6));
      break;
    case PageSize::kA4:
    default:
      size = QSizeF(mm(210.0), mm(297.0));
      break;
  }
  if (setup.orientation == PageOrientation::kLandscape) {
    size = QSizeF(size.height(), size.width());
  }
  return size;
}

PageLayoutResult PageLayoutEngine::layout(const QTextDocument& document,
                                          const PageSetup& setup) {
  const auto page = normalized(setup);
  const auto size = pageSize(page);
  const auto content = QRectF(mm(page.marginLeftMm), mm(page.marginTopMm),
                              size.width() - mm(page.marginLeftMm) -
                                  mm(page.marginRightMm),
                              size.height() - mm(page.marginTopMm) -
                                  mm(page.marginBottomMm));
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

  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    const auto rect = layout->blockBoundingRect(block);
    const auto height = rect.height();
    if (height <= 0.0) {
      continue;
    }
    const bool oversized = height > content.height();
    if (cursorY > 0.0 && cursorY + height > content.height() + 0.01) {
      ++pageIndex;
      addPage();
      cursorY = 0.0;
    }
    BlockPlacement placement;
    placement.block = block.blockNumber();
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
  }
  return result;
}

int PageLayoutEngine::pageForPosition(const QTextDocument& document,
                                      const PageSetup& setup, int position) {
  const auto result = layout(document, setup);
  const auto block = document.findBlock(position).blockNumber();
  for (const auto& placement : result.placements) {
    if (placement.block == block) {
      return placement.page;
    }
  }
  return result.pages.empty() ? 0 : int(result.pages.size()) - 1;
}

}  // namespace markpad
