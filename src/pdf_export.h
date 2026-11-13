#ifndef MARKPAD_SRC_PDF_EXPORT_H_
#define MARKPAD_SRC_PDF_EXPORT_H_

#include <QString>
#include <filesystem>
#include <optional>

#include "markdown_document.h"
#include "settings.h"

class QPrinter;
class QTextDocument;

namespace markpad {

struct PdfExportOptions {
  PageSetup page;
  QString fontFamily;
  int fontSize = 12;
};

struct PdfExportResult {
  std::optional<QString> error;
  int pageCount = 0;
  bool overflow = false;
  QString warning;
  bool success() const { return !error.has_value(); }
};

// Writes the parsed GFM document to PDF using Qt PrintSupport/QPdfWriter page
// geometry from PageSetup. Raw HTML is escaped by the shared safe renderer and
// external resources are not loaded.
PdfExportResult exportPdf(const markdown::Document& document,
                          const std::filesystem::path& path,
                          const PdfExportOptions& options);

// Writes an already rendered WYSIWYG QTextDocument using the same PagePlan.
// The document is cloned, so the caller's layout, cursor and formatting are not
// changed. Overflow is refused without silently clipping or splitting a block.
PdfExportResult exportPdfFromDocument(const QTextDocument& document,
                                      const PageSetup& page,
                                      const std::filesystem::path& path,
                                      const PdfExportOptions& options = {});

// Prints the same safe rendered document with a caller-configured QPrinter.
std::optional<QString> printDocument(const markdown::Document& document,
                                     QPrinter& printer,
                                     const PdfExportOptions& options);

std::optional<QString> printDocumentFromDocument(const QTextDocument& document,
                                                 QPrinter& printer,
                                                 const PageSetup& page);

}  // namespace markpad

#endif  // MARKPAD_SRC_PDF_EXPORT_H_
