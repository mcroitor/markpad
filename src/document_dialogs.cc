#include "document_dialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QMessageBox>

namespace markpad {
namespace {

QStringList encodingNames() {
  return {QStringLiteral("UTF-8"), QStringLiteral("UTF-16 LE"),
          QStringLiteral("UTF-16 BE"), QStringLiteral("UTF-32 LE"),
          QStringLiteral("UTF-32 BE")};
}

}  // namespace

std::filesystem::path filePath(const QString& path) {
#ifdef _WIN32
  return std::filesystem::path(path.toStdWString());
#else
  return std::filesystem::path(path.toUtf8().toStdString());
#endif
}

QString displayPath(const std::filesystem::path& path) {
#ifdef _WIN32
  return QString::fromStdWString(path.native());
#else
  return QString::fromUtf8(path.native());
#endif
}

QString DocumentDialogs::chooseOpen(QWidget* parent) {
  return QFileDialog::getOpenFileName(
      parent, QStringLiteral("Open Markdown"), {},
      QStringLiteral("Markdown (*.md *.markdown);;All files (*)"));
}

std::optional<SaveSelection> DocumentDialogs::chooseSave(QWidget* parent,
                                                         const QString& path,
                                                         FileFormat format) {
  const auto selected = QFileDialog::getSaveFileName(
      parent, QStringLiteral("Save Markdown"), path,
      QStringLiteral("Markdown (*.md);;All files (*)"));
  if (selected.isEmpty()) {
    return std::nullopt;
  }
  QDialog dialog(parent);
  dialog.setWindowTitle(QStringLiteral("File encoding"));
  auto* layout = new QFormLayout(&dialog);
  auto* encoding = new QComboBox(&dialog);
  encoding->addItems(encodingNames());
  encoding->setCurrentIndex(static_cast<int>(format.encoding));
  auto* bom =
      new QCheckBox(QStringLiteral("Write byte-order mark (BOM)"), &dialog);
  bom->setChecked(format.bom);
  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addRow(QStringLiteral("Encoding"), encoding);
  layout->addRow(bom);
  layout->addRow(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                   &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) {
    return std::nullopt;
  }
  return SaveSelection{
      selected,
      {static_cast<Encoding>(encoding->currentIndex()), bom->isChecked()}};
}

std::optional<Encoding> DocumentDialogs::chooseEncoding(QWidget* parent) {
  bool accepted = false;
  const auto names = encodingNames();
  const auto selected = QInputDialog::getItem(
      parent, QStringLiteral("Choose encoding"),
      QStringLiteral("Automatic decoding failed. Open using:"), names, 0, false,
      &accepted);
  if (!accepted) {
    return std::nullopt;
  }
  return static_cast<Encoding>(names.indexOf(selected));
}

CloseChoice DocumentDialogs::confirmClose(QWidget* parent,
                                          const QString& name) {
  const auto answer = QMessageBox::warning(
      parent, QStringLiteral("Unsaved changes"),
      QStringLiteral("Save changes to %1?").arg(name),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
      QMessageBox::Save);
  if (answer == QMessageBox::Save) {
    return CloseChoice::kSave;
  }
  return answer == QMessageBox::Discard ? CloseChoice::kDiscard
                                        : CloseChoice::kCancel;
}

void DocumentDialogs::showError(QWidget* parent, const QString& message) {
  QMessageBox::critical(parent, QStringLiteral("Markpad"), message);
}

}  // namespace markpad
