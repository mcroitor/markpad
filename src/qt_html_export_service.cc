#include "qt_html_export_service.h"

#include <QFile>
#include <QSaveFile>
#include <QStringConverter>

namespace markpad {
namespace {

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

}  // namespace

std::optional<QString> writeHtmlFile(const std::filesystem::path& path,
                                     const QString& html) {
  if (!validPath(path)) {
    return QStringLiteral("Invalid HTML export path");
  }
  const QByteArray bytes = html.toUtf8();
  QSaveFile file(qtPath(path));
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly)) {
    return QStringLiteral("Cannot open HTML export file: %1")
        .arg(file.errorString());
  }
  if (file.write(bytes) != bytes.size()) {
    const auto error = QStringLiteral("Cannot write HTML export file: %1")
                           .arg(file.errorString());
    file.cancelWriting();
    return error;
  }
  if (!file.commit()) {
    return QStringLiteral("Cannot commit HTML export file: %1")
        .arg(file.errorString());
  }
  return std::nullopt;
}

}  // namespace markpad
