#ifndef MARKPAD_DOCUMENT_SESSION_H_
#define MARKPAD_DOCUMENT_SESSION_H_

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace markpad {

enum class Encoding { kUtf8, kUtf16Le, kUtf16Be, kUtf32Le, kUtf32Be };

struct FileFormat {
  Encoding encoding = Encoding::kUtf8;
  bool bom = false;
  bool operator==(const FileFormat&) const = default;
};

enum class DocumentErrorCode {
  kInvalidText,
  kInvalidPath,
  kEncoding,
  kRead,
  kWrite,
  kUnsavedChanges,
  kSnapshotMismatch,
  kRevisionOverflow
};

struct DocumentError {
  DocumentErrorCode code;
  std::string message;
};

using DocumentStatus = std::optional<DocumentError>;

struct FileContents {
  std::string source;
  FileFormat format;
};

struct ReadResult {
  std::optional<FileContents> contents;
  DocumentStatus error;
};

class DocumentFileService {
 public:
  virtual ~DocumentFileService() = default;
  virtual ReadResult read(const std::filesystem::path& path,
                          std::optional<Encoding> encoding) = 0;
  virtual DocumentStatus write(const std::filesystem::path& path,
                               const FileContents& contents) = 0;
};

// Identity is process-local; it is not a persistent file id or authorization.
struct DocumentIdentity {};

struct DocumentToken {
  std::shared_ptr<const DocumentIdentity> identity;
  std::uint64_t revision = 0;
  bool operator==(const DocumentToken&) const = default;
};

struct DocumentSnapshot {
  std::string source;
  FileFormat format;
  DocumentToken token;
};

bool validDocumentText(std::string_view text);

// Single-threaded owner. The file service must outlive this session.
class DocumentSession {
 public:
  explicit DocumentSession(DocumentFileService& files);
  DocumentSession(const DocumentSession&) = delete;
  DocumentSession& operator=(const DocumentSession&) = delete;

  DocumentSnapshot snapshot() const;
  const std::filesystem::path& path() const { return path_; }
  bool dirty() const;
  DocumentStatus replaceSource(const DocumentToken& token, std::string source);
  DocumentStatus open(const std::filesystem::path& path,
                      std::optional<Encoding> encoding = std::nullopt,
                      bool discardChanges = false);
  DocumentStatus save();
  DocumentStatus saveAs(const std::filesystem::path& path,
                        std::optional<FileFormat> format = std::nullopt);

 private:
  DocumentFileService& files_;
  DocumentToken token_;
  FileContents current_;
  FileContents saved_;
  std::filesystem::path path_;
};

}  // namespace markpad

#endif  // MARKPAD_DOCUMENT_SESSION_H_
