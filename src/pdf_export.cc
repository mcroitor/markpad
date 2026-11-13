#include "pdf_export.h"

#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPrinter>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QTextDocument>
#include <QUrl>
#include <algorithm>
#include <memory>

#include "html_export.h"
#include "page_layout.h"

namespace markpad {
namespace {

constexpr double kMmToPoints = 72.0 / 25.4;
// QTextDocument lays out point-sized fonts at 96 DPI. Using the same logical
// scale for screen canvas and PDF keeps line wrapping and page breaks equal;
// the painter then scales the content to the real PDF page.
constexpr qreal kLayoutScale = 96.0 / 72.0;

class SafePrintDocument final : public QTextDocument {
 public:
  QVariant loadResource(int, const QUrl&) override { return {}; }
};

QString qtPath(const std::filesystem::path& path) {
#ifdef _WIN32
  return QString::fromStdWString(path.native());
#else
  return QFile::decodeName(QByteArray::fromStdString(path.native()));
#endif
}

bool validPath(const std::filesystem::path& path) {
  const auto converted = qtPath(path);
  if (path.empty() || converted.contains(QChar(u'\0')) ||
      !converted.isValidUtf16()) {
    return false;
  }
#ifndef _WIN32
  if (QFile::encodeName(converted).toStdString() != path.native()) {
    return false;
  }
#endif
  return true;
}

QPageSize pageSize(PageSize size) {
  switch (size) {
    case PageSize::kLetter:
      return QPageSize(QPageSize::Letter);
    case PageSize::kLegal:
      return QPageSize(QPageSize::Legal);
    case PageSize::kA4:
    default:
      return QPageSize(QPageSize::A4);
  }
}

QPageLayout pageLayout(const PageSetup& page) {
  return QPageLayout(
      pageSize(page.size),
      page.orientation == PageOrientation::kLandscape
          ? QPageLayout::Landscape
          : QPageLayout::Portrait,
      QMarginsF(page.marginLeftMm, page.marginTopMm, page.marginRightMm,
                page.marginBottomMm),
      QPageLayout::Millimeter);
}

bool configurePageLayout(QPrinter& printer, const PageSetup& page) {
  return printer.setPageLayout(pageLayout(page));
}

bool configurePageLayout(QPdfWriter& writer, const PageSetup& page) {
  return writer.setPageLayout(pageLayout(page));
}

QString overflowError(const PagePlan& plan) {
  QString block;
  for (const auto& unit : plan.units) {
    if (unit.overflow) {
      block = QStringLiteral(" (block %1)").arg(unit.firstBlock);
      break;
    }
  }
  const auto reason = plan.warning.isEmpty()
                          ? QStringLiteral("a block does not fit the page")
                          : plan.warning;
  return QStringLiteral("PDF export refused: %1%2").arg(reason, block);
}

void prepareDocument(QTextDocument& document, const PageSetup& page) {
  const auto size = PageLayoutEngine::pageSize(page, kLayoutScale);
  const auto width =
      size.width() -
      (page.marginLeftMm + page.marginRightMm) * kMmToPoints * kLayoutScale;
  document.setDocumentMargin(0);
  if (width > 0.0) {
    document.setTextWidth(width);
  }
}

// Draws the shared PagePlan page by page. The device must already have a page
// layout matching the plan; each page is clipped to its content area and units
// are never split.
template <typename Device>
bool paintPlan(QPainter& painter, Device& device, QTextDocument& document,
               const PagePlan& plan) {
  if (plan.contentRect.width() <= 0.0 || plan.contentRect.height() <= 0.0) {
    return false;
  }
  const auto dpi = device.logicalDpiX();
  for (size_t index = 0; index < plan.pages.size(); ++index) {
    if (index > 0 && !device.newPage()) {
      return false;
    }
    const auto paint = device.pageLayout().paintRectPixels(dpi);
    if (paint.width() <= 0 || paint.height() <= 0) {
      return false;
    }
    painter.save();
    painter.setClipRect(paint);
    painter.translate(paint.topLeft());
    painter.scale(paint.width() / plan.contentRect.width(),
                  paint.height() / plan.contentRect.height());
    QAbstractTextDocumentLayout::PaintContext context;
    context.clip = QRectF(0, plan.pages[index].documentTop,
                          plan.contentRect.width(), plan.contentRect.height());
    painter.translate(0, -plan.pages[index].documentTop);
    document.documentLayout()->draw(&painter, context);
    painter.restore();
  }
  return true;
}

std::unique_ptr<SafePrintDocument> createDocument(
    const markdown::Document& document, const PdfExportOptions& options,
    QString& error) {
  const auto rendered = markdown::renderHtmlFragment(document);
  if (!rendered.html) {
    error = rendered.error;
    return nullptr;
  }
  auto output = std::make_unique<SafePrintDocument>();
  QFont font = options.fontFamily.trimmed().isEmpty()
                   ? QFontDatabase::systemFont(QFontDatabase::GeneralFont)
                   : QFont(options.fontFamily);
  font.setPointSize(std::max(1, options.fontSize));
  output->setDefaultFont(font);
  prepareDocument(*output, options.page);
  output->setHtml(*rendered.html);
  return output;
}

PdfExportResult failure(QString error) {
  PdfExportResult result;
  result.error = std::move(error);
  return result;
}

PdfExportResult exportPrepared(QTextDocument& document, const PageSetup& page,
                               const std::filesystem::path& path) {
  if (!validPath(path)) {
    return failure(QStringLiteral("Unsupported PDF export path"));
  }
  const auto plan = PagePlanner::plan(document, page, kLayoutScale);
  if (plan.overflow) {
    PdfExportResult result;
    result.error = overflowError(plan);
    result.overflow = true;
    result.warning = plan.warning;
    return result;
  }
  const auto target = qtPath(path);
  const auto directory = QFileInfo(target).absolutePath();
  QTemporaryFile temporary(
      QDir(directory).filePath(QStringLiteral("markpad-pdf-XXXXXX.pdf")));
  temporary.setAutoRemove(false);
  if (!temporary.open()) {
    return failure(QStringLiteral("Cannot create PDF temporary file: %1")
                       .arg(temporary.errorString()));
  }
  const auto temporaryPath = temporary.fileName();
  temporary.close();

  QPdfWriter writer(temporaryPath);
  writer.setResolution(300);
  if (!configurePageLayout(writer, page)) {
    QFile::remove(temporaryPath);
    return failure(QStringLiteral("Invalid PDF page setup"));
  }
  QPainter painter;
  if (!painter.begin(&writer)) {
    QFile::remove(temporaryPath);
    return failure(QStringLiteral("Cannot start PDF writer"));
  }
  const bool painted = paintPlan(painter, writer, document, plan);
  painter.end();
  if (!painted) {
    QFile::remove(temporaryPath);
    return failure(QStringLiteral("Cannot render PDF page"));
  }
  const QFileInfo printed(temporaryPath);
  if (!printed.exists() || printed.size() <= 0) {
    QFile::remove(temporaryPath);
    return failure(QStringLiteral("PDF writer did not produce output"));
  }
  QFile source(temporaryPath);
  if (!source.open(QIODevice::ReadOnly)) {
    QFile::remove(temporaryPath);
    return failure(QStringLiteral("Cannot read generated PDF: %1")
                       .arg(source.errorString()));
  }
  QSaveFile output(target);
  output.setDirectWriteFallback(false);
  if (!output.open(QIODevice::WriteOnly)) {
    QFile::remove(temporaryPath);
    return failure(QStringLiteral("Cannot write PDF target: %1")
                       .arg(output.errorString()));
  }
  while (!source.atEnd()) {
    const auto chunk = source.read(1024 * 1024);
    if (chunk.isEmpty() && source.error() != QFileDevice::NoError) {
      output.cancelWriting();
      QFile::remove(temporaryPath);
      return failure(QStringLiteral("Cannot read generated PDF: %1")
                         .arg(source.errorString()));
    }
    if (output.write(chunk) != chunk.size()) {
      output.cancelWriting();
      QFile::remove(temporaryPath);
      return failure(QStringLiteral("Cannot write PDF target: %1")
                         .arg(output.errorString()));
    }
  }
  if (!output.commit()) {
    QFile::remove(temporaryPath);
    return failure(QStringLiteral("Cannot commit PDF target: %1")
                       .arg(output.errorString()));
  }
  QFile::remove(temporaryPath);
  PdfExportResult result;
  result.pageCount = int(plan.pages.size());
  result.warning = plan.warning;
  return result;
}

std::optional<QString> printPrepared(QTextDocument& document, QPrinter& printer,
                                     const PageSetup& page) {
  if (!configurePageLayout(printer, page)) {
    return QStringLiteral("Invalid print page setup");
  }
  const auto plan = PagePlanner::plan(document, page, kLayoutScale);
  if (plan.overflow) {
    return overflowError(plan);
  }
  QPainter painter;
  if (!painter.begin(&printer)) {
    return QStringLiteral("Cannot start printing");
  }
  const bool painted = paintPlan(painter, printer, document, plan);
  painter.end();
  if (!painted) {
    return QStringLiteral("Cannot render print page");
  }
  if (printer.printerState() == QPrinter::Error ||
      printer.printerState() == QPrinter::Aborted) {
    return QStringLiteral("Printing was cancelled or failed");
  }
  return std::nullopt;
}

}  // namespace

PdfExportResult exportPdf(const markdown::Document& document,
                          const std::filesystem::path& path,
                          const PdfExportOptions& options) {
  QString documentError;
  auto printable = createDocument(document, options, documentError);
  if (!printable) {
    return failure(documentError);
  }
  return exportPrepared(*printable, options.page, path);
}

PdfExportResult exportPdfFromDocument(const QTextDocument& document,
                                      const PageSetup& page,
                                      const std::filesystem::path& path,
                                      const PdfExportOptions&) {
  if (!validPath(path)) {
    return failure(QStringLiteral("Unsupported PDF export path"));
  }
  auto copy = std::unique_ptr<QTextDocument>(document.clone());
  prepareDocument(*copy, page);
  return exportPrepared(*copy, page, path);
}

std::optional<QString> printDocument(const markdown::Document& document,
                                     QPrinter& printer,
                                     const PdfExportOptions& options) {
  QString documentError;
  auto printable = createDocument(document, options, documentError);
  if (!printable) {
    return documentError;
  }
  return printPrepared(*printable, printer, options.page);
}

std::optional<QString> printDocumentFromDocument(const QTextDocument& document,
                                                 QPrinter& printer,
                                                 const PageSetup& page) {
  auto copy = std::unique_ptr<QTextDocument>(document.clone());
  prepareDocument(*copy, page);
  return printPrepared(*copy, printer, page);
}

}  // namespace markpad
