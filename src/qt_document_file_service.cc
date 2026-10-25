#include "qt_document_file_service.h"

#include <QFile>
#include <QSaveFile>
#include <QStringConverter>
#include <array>
#include <limits>
#include <vector>

namespace markpad {
namespace {

struct Codec {
  Encoding encoding;
  QStringConverter::Encoding qtEncoding;
  QByteArray bom;
  qsizetype width;
  bool littleEndian;
};

const std::array<Codec, 5>& codecs() {
  static const std::array<Codec, 5> values{{
      {Encoding::kUtf32Le, QStringConverter::Utf32LE,
       QByteArray::fromHex("fffe0000"), 4, true},
      {Encoding::kUtf32Be, QStringConverter::Utf32BE,
       QByteArray::fromHex("0000feff"), 4, false},
      {Encoding::kUtf8, QStringConverter::Utf8, QByteArray::fromHex("efbbbf"),
       1, false},
      {Encoding::kUtf16Le, QStringConverter::Utf16LE,
       QByteArray::fromHex("fffe"), 2, true},
      {Encoding::kUtf16Be, QStringConverter::Utf16BE,
       QByteArray::fromHex("feff"), 2, false},
  }};
  return values;
}

const Codec* codecFor(Encoding encoding) {
  for (const auto& codec : codecs()) {
    if (codec.encoding == encoding) {
      return &codec;
    }
  }
  return nullptr;
}

std::optional<std::string> decode(const QByteArray& bytes, const Codec& codec) {
  if (bytes.size() % codec.width != 0) {
    return std::nullopt;
  }
  QStringDecoder decoder(codec.qtEncoding,
                         QStringConverter::Flag::Stateless |
                             QStringConverter::Flag::ConvertInitialBom);
  const QString text = decoder(bytes);
  if (decoder.hasError() || !text.isValidUtf16() ||
      text.contains(QChar(u'\0'))) {
    return std::nullopt;
  }
  QStringEncoder encoder(codec.qtEncoding, QStringConverter::Flag::Stateless);
  const QByteArray roundTrip = encoder(text);
  if (encoder.hasError() || roundTrip != bytes) {
    return std::nullopt;
  }
  return text.toUtf8().toStdString();
}

bool matchesZeroPattern(const QByteArray& bytes, const Codec& codec) {
  if (bytes.isEmpty() || bytes.size() % codec.width != 0) {
    return false;
  }
  const auto units = bytes.size() / codec.width;
  qsizetype matches = 0;
  for (qsizetype offset = 0; offset < bytes.size(); offset += codec.width) {
    bool highBytesZero = true;
    const auto lowLane = codec.littleEndian ? 0 : codec.width - 1;
    for (qsizetype lane = 0; lane < codec.width; ++lane) {
      if (lane != lowLane && bytes[offset + lane] != '\0') {
        highBytesZero = false;
      }
    }
    if (highBytesZero && bytes[offset + lowLane] != '\0') {
      ++matches;
    }
  }
  return matches > 0 && matches >= units / 2 + units % 2;
}

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

DocumentError failure(DocumentErrorCode code, const QString& message) {
  return {code, message.toUtf8().toStdString()};
}

}  // namespace

ReadResult QtDocumentFileService::read(const std::filesystem::path& path,
                                       std::optional<Encoding> encoding) {
  if (!validPath(path)) {
    return {std::nullopt, DocumentError{DocumentErrorCode::kInvalidPath,
                                        "Invalid file path"}};
  }
  if (encoding && !codecFor(*encoding)) {
    return {std::nullopt,
            DocumentError{DocumentErrorCode::kEncoding, "Unknown encoding"}};
  }
  QFile file(qtPath(path));
  if (!file.open(QIODevice::ReadOnly)) {
    return {std::nullopt,
            failure(DocumentErrorCode::kRead, file.errorString())};
  }
  auto bytes = file.readAll();
  if (file.error() != QFileDevice::NoError) {
    return {std::nullopt,
            failure(DocumentErrorCode::kRead, file.errorString())};
  }
  for (const auto& codec : codecs()) {
    if (!bytes.startsWith(codec.bom)) {
      continue;
    }
    if (encoding && *encoding != codec.encoding) {
      return {std::nullopt,
              DocumentError{DocumentErrorCode::kEncoding,
                            "Explicit encoding conflicts with BOM"}};
    }
    auto source = decode(bytes.mid(codec.bom.size()), codec);
    if (!source) {
      return {std::nullopt, DocumentError{DocumentErrorCode::kEncoding,
                                          "Invalid Unicode text after BOM"}};
    }
    return {FileContents{std::move(*source), {codec.encoding, true}},
            std::nullopt};
  }
  if (encoding) {
    auto source = decode(bytes, *codecFor(*encoding));
    if (!source) {
      return {std::nullopt,
              DocumentError{DocumentErrorCode::kEncoding,
                            "Invalid text for explicit encoding"}};
    }
    return {FileContents{std::move(*source), {*encoding, false}}, std::nullopt};
  }
  auto utf8 = decode(bytes, *codecFor(Encoding::kUtf8));
  if (utf8) {
    return {FileContents{std::move(*utf8), {}}, std::nullopt};
  }
  std::vector<FileContents> candidates;
  for (const auto& codec : codecs()) {
    if (codec.width == 1 || !matchesZeroPattern(bytes, codec)) {
      continue;
    }
    if (auto source = decode(bytes, codec)) {
      candidates.push_back({std::move(*source), {codec.encoding, false}});
    }
  }
  if (candidates.size() != 1) {
    return {std::nullopt,
            DocumentError{DocumentErrorCode::kEncoding,
                          "Encoding detection uncertain; specify encoding"}};
  }
  return {std::move(candidates.front()), std::nullopt};
}

DocumentStatus QtDocumentFileService::write(const std::filesystem::path& path,
                                            const FileContents& contents) {
  if (!validPath(path)) {
    return DocumentError{DocumentErrorCode::kInvalidPath, "Invalid file path"};
  }
  const auto* codec = codecFor(contents.format.encoding);
  if (!codec) {
    return DocumentError{DocumentErrorCode::kEncoding, "Unknown encoding"};
  }
  if (!validDocumentText(contents.source) ||
      contents.source.size() >
          static_cast<std::size_t>(std::numeric_limits<qsizetype>::max())) {
    return DocumentError{DocumentErrorCode::kInvalidText,
                         "Document text must be strict UTF-8 without U+0000"};
  }
  QStringDecoder utf8(QStringConverter::Utf8,
                      QStringConverter::Flag::Stateless |
                          QStringConverter::Flag::ConvertInitialBom);
  const QString text = utf8(QByteArrayView(
      contents.source.data(), static_cast<qsizetype>(contents.source.size())));
  QStringEncoder encoder(codec->qtEncoding, QStringConverter::Flag::Stateless);
  QByteArray bytes = encoder(text);
  if (encoder.hasError()) {
    return DocumentError{DocumentErrorCode::kEncoding,
                         "Text cannot be represented in target encoding"};
  }
  if (!contents.format.bom && text.startsWith(QChar(0xfeff))) {
    return DocumentError{
        DocumentErrorCode::kEncoding,
        "Leading U+FEFF requires a transport BOM to preserve document text"};
  }
  if (contents.format.bom) {
    bytes.prepend(codec->bom);
  }
  QSaveFile file(qtPath(path));
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly)) {
    return failure(DocumentErrorCode::kWrite, file.errorString());
  }
  if (file.write(bytes) != bytes.size()) {
    const auto error = failure(DocumentErrorCode::kWrite, file.errorString());
    file.cancelWriting();
    return error;
  }
  if (!file.commit()) {
    return failure(DocumentErrorCode::kWrite, file.errorString());
  }
  return std::nullopt;
}

}  // namespace markpad
