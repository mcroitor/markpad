#include "document_session.h"

#include <limits>
#include <utility>

namespace markpad {
namespace {

std::optional<std::filesystem::path> absolutePath(
    const std::filesystem::path& path) {
  if (path.empty()) {
    return std::nullopt;
  }
  std::error_code error;
  auto resolved = std::filesystem::absolute(path, error);
  if (error) {
    return std::nullopt;
  }
  return resolved;
}

}  // namespace

bool validDocumentText(std::string_view text) {
  for (std::size_t i = 0; i < text.size();) {
    const auto first = static_cast<unsigned char>(text[i++]);
    if (first == 0) {
      return false;
    }
    if (first < 0x80) {
      continue;
    }
    unsigned count;
    std::uint32_t value;
    std::uint32_t minimum;
    if (first >= 0xc2 && first <= 0xdf) {
      count = 1;
      value = first & 0x1f;
      minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
      count = 2;
      value = first & 0x0f;
      minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
      count = 3;
      value = first & 0x07;
      minimum = 0x10000;
    } else {
      return false;
    }
    if (count > text.size() - i) {
      return false;
    }
    for (unsigned j = 0; j < count; ++j) {
      const auto next = static_cast<unsigned char>(text[i++]);
      if ((next & 0xc0) != 0x80) {
        return false;
      }
      value = (value << 6) | (next & 0x3f);
    }
    if (value < minimum || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff)) {
      return false;
    }
  }
  return true;
}

DocumentSession::DocumentSession(DocumentFileService& files)
    : files_(files), token_{std::make_shared<DocumentIdentity>(), 0} {}

DocumentSnapshot DocumentSession::snapshot() const {
  return {current_.source, current_.format, token_};
}

bool DocumentSession::dirty() const {
  return current_.source != saved_.source || current_.format != saved_.format;
}

DocumentStatus DocumentSession::replaceSource(const DocumentToken& token,
                                              std::string source) {
  if (token != token_) {
    return DocumentError{DocumentErrorCode::kSnapshotMismatch,
                         "Stale or foreign document snapshot"};
  }
  if (!validDocumentText(source)) {
    return DocumentError{DocumentErrorCode::kInvalidText,
                         "Document text must be strict UTF-8 without U+0000"};
  }
  if (source == current_.source) {
    return std::nullopt;
  }
  if (token_.revision == std::numeric_limits<std::uint64_t>::max()) {
    return DocumentError{DocumentErrorCode::kRevisionOverflow,
                         "Document revision overflow"};
  }
  current_.source = std::move(source);
  ++token_.revision;
  return std::nullopt;
}

DocumentStatus DocumentSession::open(const std::filesystem::path& path,
                                     std::optional<Encoding> encoding,
                                     bool discardChanges) {
  if (dirty() && !discardChanges) {
    return DocumentError{DocumentErrorCode::kUnsavedChanges,
                         "Opening would discard unsaved changes"};
  }
  const auto resolved = absolutePath(path);
  if (!resolved) {
    return DocumentError{DocumentErrorCode::kInvalidPath,
                         "File path is empty or cannot be resolved"};
  }
  auto result = files_.read(*resolved, encoding);
  if (result.error) {
    return result.error;
  }
  if (!result.contents || !validDocumentText(result.contents->source)) {
    return DocumentError{DocumentErrorCode::kInvalidText,
                         "File service did not return valid document text"};
  }
  current_ = std::move(*result.contents);
  saved_ = current_;
  path_ = *resolved;
  token_ = {std::make_shared<DocumentIdentity>(), 0};
  return std::nullopt;
}

DocumentStatus DocumentSession::save() { return saveAs(path_); }

DocumentStatus DocumentSession::saveAs(const std::filesystem::path& path,
                                       std::optional<FileFormat> format) {
  const auto resolved = absolutePath(path);
  if (!resolved) {
    return DocumentError{DocumentErrorCode::kInvalidPath,
                         "File path is empty or cannot be resolved"};
  }
  auto contents = current_;
  if (format) {
    contents.format = *format;
  }
  if (auto error = files_.write(*resolved, contents)) {
    return error;
  }
  path_ = *resolved;
  current_.format = contents.format;
  saved_ = current_;
  return std::nullopt;
}

}  // namespace markpad
