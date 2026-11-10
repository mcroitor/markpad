#ifndef MARKPAD_SRC_DOCUMENT_DIALOGS_H_
#define MARKPAD_SRC_DOCUMENT_DIALOGS_H_

#include <QString>
#include <optional>

#include "document_session.h"

class QWidget;

namespace markpad {

struct SaveSelection {
  QString path;
  FileFormat format;
};

enum class CloseChoice { kSave, kDiscard, kCancel };

// Override in GUI tests to avoid native modal dialogs.
class DocumentDialogs {
 public:
  virtual ~DocumentDialogs() = default;
  void setDefaultLocation(const QString& path) { defaultLocation_ = path; }
  QString defaultLocation() const { return defaultLocation_; }
  virtual QString chooseOpen(QWidget* parent);
  virtual std::optional<SaveSelection> chooseSave(QWidget* parent,
                                                  const QString& path,
                                                  FileFormat format);
  virtual std::optional<Encoding> chooseEncoding(QWidget* parent);
  virtual std::optional<QString> chooseLink(QWidget* parent);
  virtual CloseChoice confirmClose(QWidget* parent, const QString& name);
  virtual void showError(QWidget* parent, const QString& message);

 private:
  QString defaultLocation_;
};

std::filesystem::path filePath(const QString& path);
QString displayPath(const std::filesystem::path& path);

}  // namespace markpad

#endif  // MARKPAD_SRC_DOCUMENT_DIALOGS_H_
