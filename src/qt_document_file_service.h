#ifndef MARKPAD_QT_DOCUMENT_FILE_SERVICE_H_
#define MARKPAD_QT_DOCUMENT_FILE_SERVICE_H_

#include "document_session.h"

namespace markpad {

class QtDocumentFileService final : public DocumentFileService {
 public:
  ReadResult read(const std::filesystem::path& path,
                  std::optional<Encoding> encoding = std::nullopt) override;
  DocumentStatus write(const std::filesystem::path& path,
                       const FileContents& contents) override;
};

}  // namespace markpad

#endif  // MARKPAD_QT_DOCUMENT_FILE_SERVICE_H_
