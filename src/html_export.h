#ifndef MARKPAD_SRC_HTML_EXPORT_H_
#define MARKPAD_SRC_HTML_EXPORT_H_

#include <QString>
#include <optional>

#include "markdown_document.h"

namespace markpad::markdown {

struct HtmlRenderResult {
  std::optional<QString> html;
  QString error;
};

// Renders a parsed GFM document into a self-contained, conservative HTML
// document. Raw HTML, scripts and dangerous URI schemes are escaped or
// omitted; no external resource is fetched.
HtmlRenderResult renderHtml(const Document& document);

// Renders only the body fragment, without <html>/<head>/<style>. Intended for
// in-process printing/PDF conversion, where the same safe policy applies.
HtmlRenderResult renderHtmlFragment(const Document& document);

}  // namespace markpad::markdown

#endif  // MARKPAD_SRC_HTML_EXPORT_H_
