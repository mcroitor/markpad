#include "pdf_export.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QPageLayout>
#include <QPageSize>
#include <QPrinter>
#include <QTemporaryFile>
#include <QTextDocument>
#include <QUrl>
#include <algorithm>
#include <memory>

#include "html_export.h"
#include "page_layout.h"

namespace markpad {
namespace {

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

bool configurePageLayout(QPrinter& printer, const PageSetup& page) {
  const auto margins = QMarginsF(page.marginLeftMm, page.marginTopMm,
                                 page.marginRightMm, page.marginBottomMm);
  const QPageLayout layout(
      pageSize(page.size),
      page.orientation == PageOrientation::kLandscape
          ? QPageLayout::Landscape
          : QPageLayout::Portrait,
      margins, QPageLayout::Millimeter);
  return printer.setPageLayout(layout);
}

std::optional<std::unique_ptr<SafePrintDocument>> createDocument(
    const markdown::Document& document, const PdfExportOptions& options,
    QString& error) {
  const auto rendered = markdown::renderHtmlFragment(document);
  if (!rendered.html) {
    error = rendered.error;
    return std::nullopt;
  }
  auto output = std::make_unique<SafePrintDocument>();
  QFont font = options.fontFamily.trimmed().isEmpty()
                   ? QFontDatabase::systemFont(QFontDatabase::GeneralFont)
                   : QFont(options.fontFamily);
  font.setPointSize(std::max(1, options.fontSize));
  output->setDefaultFont(font);
  output->setDocumentMargin(0);
  const auto page = PageLayoutEngine::pageSize(options.page);
  const auto width =
      page.width() -
      (options.page.marginLeftMm + options.page.marginRightMm) * 72.0 / 25.4;
  if (width > 0.0) {
    output->setTextWidth(width);
  }
  output->setHtml(*rendered.html);
  return output;
}

PdfExportResult failure(QString error) {
  PdfExportResult result;
  result.error = std::move(error);
  return result;
}

}  // namespace

PdfExportResult exportPdf(const markdown::Document& document,
                          const std::filesystem::path& path,
                          const PdfExportOptions& options) {
  if (!validPath(path)) {
    return failure(QStringLiteral("Invalid PDF export path"));
  }
  QString documentError;
  auto printable = createDocument(document, options, documentError);
  if (!printable) {
    return failure(documentError);
  }
  const auto layout = PageLayoutEngine::layout(**printable, options.page);
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

  QPrinter printer(QPrinter::HighResolution);
  printer.setOutputFormat(QPrinter::PdfFormat);
  printer.setOutputFileName(temporaryPath);
  printer.setResolution(300);
  if (!configurePageLayout(printer, options.page)) {
    QFile::remove(temporaryPath);
    return failure(QStringLiteral("Invalid PDF page setup"));
  }
  (*printable)->print(&printer);
  const QFileInfo printed(temporaryPath);
  if (!printed.exists() || printed.size() <= 0) {
    QFile::remove(temporaryPath);
    return failure(QStringLiteral("PDF printer did not produce output"));
  }
  if (QFileInfo::exists(target) && !QFile::remove(target)) {
    QFile::remove(temporaryPath);
    return failure(QStringLiteral("Cannot replace existing PDF file"));
  }
  if (!QFile::copy(temporaryPath, target)) {
    QFile::remove(temporaryPath);
    return failure(QStringLiteral("Cannot move PDF into target path"));
  }
  QFile::remove(temporaryPath);
  PdfExportResult result;
  result.pageCount = int(layout.pages.size());
  result.overflow = layout.overflow;
  result.warning = layout.warning;
  return result;
}

std::optional<QString> printDocument(const markdown::Document& document,
                                     QPrinter& printer,
                                     const PdfExportOptions& options) {
  QString documentError;
  auto printable = createDocument(document, options, documentError);
  if (!printable) {
    return documentError;
  }
  if (!configurePageLayout(printer, options.page)) {
    return QStringLiteral("Invalid print page setup");
  }
  (*printable)->print(&printer);
  if (printer.printerState() == QPrinter::Error ||
      printer.printerState() == QPrinter::Aborted) {
    return QStringLiteral("Printing was cancelled or failed");
  }
  return std::nullopt;
}

}  // namespace markpad
