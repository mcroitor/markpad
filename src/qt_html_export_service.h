#ifndef MARKPAD_SRC_QT_HTML_EXPORT_SERVICE_H_
#define MARKPAD_SRC_QT_HTML_EXPORT_SERVICE_H_

#include <QString>
#include <filesystem>
#include <optional>

namespace markpad {

// Writes an HTML document as strict UTF-8 using QSaveFile. Returns an error
// message when the file cannot be written or committed.
std::optional<QString> writeHtmlFile(const std::filesystem::path& path,
                                     const QString& html);

}  // namespace markpad

#endif  // MARKPAD_SRC_QT_HTML_EXPORT_SERVICE_H_
