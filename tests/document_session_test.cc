#include "document_session.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "qt_document_file_service.h"

namespace markpad {
namespace {

std::filesystem::path nativePath(const QString& path) {
#ifdef _WIN32
  return std::filesystem::path(path.toStdWString());
#else
  return std::filesystem::path(path.toUtf8().toStdString());
#endif
}

class FakeFiles : public DocumentFileService {
 public:
  ReadResult nextRead{FileContents{"loaded", {}}, std::nullopt};
  DocumentStatus writeError;
  int writes = 0;
  ReadResult read(const std::filesystem::path&,
                  std::optional<Encoding>) override {
    return nextRead;
  }
  DocumentStatus write(const std::filesystem::path&,
                       const FileContents&) override {
    ++writes;
    return writeError;
  }
};

}  // namespace

class DocumentSessionTest : public QObject {
  Q_OBJECT

 private slots:
  void sessionTransitions() {
    FakeFiles files;
    DocumentSession session(files);
    const auto initial = session.snapshot();
    QVERIFY(!session.dirty());
    QVERIFY(session.path().empty());
    QVERIFY(session.save());
    QVERIFY(!session.replaceSource(initial.token, "hello\r\n"));
    QVERIFY(session.dirty());
    QCOMPARE(session.snapshot().token.revision, std::uint64_t{1});
    QCOMPARE(initial.source, std::string{});
    QVERIFY(session.replaceSource(initial.token, "stale"));
    QVERIFY(!session.replaceSource(session.snapshot().token, "hello\r\n"));
    QCOMPARE(session.snapshot().token.revision, std::uint64_t{1});
    QVERIFY(!session.replaceSource(session.snapshot().token, ""));
    QVERIFY(!session.dirty());
    QCOMPARE(session.snapshot().token.revision, std::uint64_t{2});
    QVERIFY(session.replaceSource(initial.token, "ABA"));
    DocumentSession foreign(files);
    QVERIFY(session.replaceSource(foreign.snapshot().token, "foreign"));
    QVERIFY(!session.replaceSource(session.snapshot().token, "saved"));
    QVERIFY(!session.saveAs("first.md"));
    QVERIFY(session.path().is_absolute());
    QCOMPARE(files.writes, 1);
    QVERIFY(!session.dirty());
    QVERIFY(!session.replaceSource(session.snapshot().token, "changed"));
    QVERIFY(session.dirty());
    QVERIFY(!session.replaceSource(session.snapshot().token, "saved"));
    QVERIFY(!session.dirty());
    const auto beforeOpen = session.snapshot();
    QVERIFY(!session.open("second.md"));
    QCOMPARE(session.snapshot().source, std::string("loaded"));
    QCOMPARE(session.snapshot().token.revision, std::uint64_t{0});
    QVERIFY(session.snapshot().token.identity != beforeOpen.token.identity);
    QVERIFY(!session.dirty());
  }

  void failedOperationsAreAtomic() {
    FakeFiles files;
    DocumentSession session(files);
    QVERIFY(!session.open("first.md"));
    QVERIFY(!session.replaceSource(session.snapshot().token, "dirty"));
    const auto before = session.snapshot();
    const auto originalPath = session.path();
    const auto guarded = session.open("second.md");
    QVERIFY(guarded);
    QCOMPARE(guarded->code, DocumentErrorCode::kUnsavedChanges);
    files.nextRead = {std::nullopt,
                      DocumentError{DocumentErrorCode::kRead, "read failed"}};
    QVERIFY(session.open("second.md", std::nullopt, true));
    for (const auto message : {"open failed", "short write", "commit failed"}) {
      files.writeError = DocumentError{DocumentErrorCode::kWrite, message};
      QVERIFY(session.saveAs("other.md", FileFormat{Encoding::kUtf32Be, true}));
      QCOMPARE(session.snapshot().source, before.source);
      QCOMPARE(session.snapshot().format, before.format);
      QCOMPARE(session.snapshot().token, before.token);
      QCOMPARE(session.path(), originalPath);
      QVERIFY(session.dirty());
    }
    files.nextRead = {FileContents{"invalid\0text", {}}, std::nullopt};
    files.nextRead.contents->source = std::string("invalid\0text", 12);
    QVERIFY(session.open("second.md", std::nullopt, true));
    QCOMPARE(session.snapshot().source, before.source);
    files.nextRead = {FileContents{"new", {Encoding::kUtf16Be, true}},
                      std::nullopt};
    QVERIFY(!session.open("second.md", std::nullopt, true));
    QVERIFY(!session.dirty());
    QCOMPARE(session.snapshot().format, (FileFormat{Encoding::kUtf16Be, true}));
    QVERIFY(session.snapshot().token.identity != before.token.identity);
  }

  void strictText_data() {
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<bool>("valid");
    QTest::newRow("empty") << QByteArray() << true;
    QTest::newRow("unicode") << QByteArray::fromHex("d09ff09f9880") << true;
    QTest::newRow("nul") << QByteArray::fromHex("610062") << false;
    QTest::newRow("truncated") << QByteArray::fromHex("f09f98") << false;
    QTest::newRow("overlong") << QByteArray::fromHex("c080") << false;
    QTest::newRow("surrogate") << QByteArray::fromHex("eda080") << false;
    QTest::newRow("too-large") << QByteArray::fromHex("f4908080") << false;
    QTest::newRow("continuation") << QByteArray::fromHex("80") << false;
  }

  void strictText() {
    QFETCH(QByteArray, bytes);
    QFETCH(bool, valid);
    QCOMPARE(validDocumentText(bytes.toStdString()), valid);
    FakeFiles files;
    DocumentSession session(files);
    const auto error =
        session.replaceSource(session.snapshot().token, bytes.toStdString());
    QCOMPARE(!error, valid);
    if (error) {
      QCOMPARE(error->code, DocumentErrorCode::kInvalidText);
      QVERIFY(!session.dirty());
    }
  }

  void unicodeFiles_data() {
    QTest::addColumn<int>("encoding");
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QByteArray>("bom");
    QTest::newRow("utf8") << int(Encoding::kUtf8)
                          << QByteArray::fromHex("41d09ff09f98800d0a")
                          << QByteArray::fromHex("efbbbf");
    QTest::newRow("utf16le") << int(Encoding::kUtf16Le)
                             << QByteArray::fromHex("41001f043dd800de0d000a00")
                             << QByteArray::fromHex("fffe");
    QTest::newRow("utf16be") << int(Encoding::kUtf16Be)
                             << QByteArray::fromHex("0041041fd83dde00000d000a")
                             << QByteArray::fromHex("feff");
    QTest::newRow("utf32le")
        << int(Encoding::kUtf32Le)
        << QByteArray::fromHex("410000001f04000000f601000d0000000a000000")
        << QByteArray::fromHex("fffe0000");
    QTest::newRow("utf32be")
        << int(Encoding::kUtf32Be)
        << QByteArray::fromHex("000000410000041f0001f6000000000d0000000a")
        << QByteArray::fromHex("0000feff");
  }

  void unicodeFiles() {
    QFETCH(int, encoding);
    QFETCH(QByteArray, body);
    QFETCH(QByteArray, bom);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto name =
        dir.filePath(QStringLiteral("\u0442\u0435\u0441\u0442.md"));
    const auto path = nativePath(name);
    const auto codec = static_cast<Encoding>(encoding);
    const auto expected =
        QByteArray::fromHex("41d09ff09f98800d0a").toStdString();
    QtDocumentFileService files;
    for (bool withBom : {false, true}) {
      const QByteArray input = (withBom ? bom : QByteArray()) + body;
      QFile file(name);
      QVERIFY(file.open(QIODevice::WriteOnly));
      QCOMPARE(file.write(input), input.size());
      file.close();
      auto result = files.read(
          path, withBom ? std::nullopt : std::optional<Encoding>(codec));
      QVERIFY2(result.contents,
               result.error ? result.error->message.c_str() : "");
      QCOMPARE(result.contents->source, expected);
      QCOMPARE(result.contents->format, (FileFormat{codec, withBom}));
      QVERIFY(!files.write(path, *result.contents));
      QVERIFY(file.open(QIODevice::ReadOnly));
      QCOMPARE(file.readAll(), input);
      file.close();
      DocumentSession session(files);
      QVERIFY(!session.open(path, codec));
      QVERIFY(!session.replaceSource(session.snapshot().token, expected + "x"));
      QVERIFY(!session.save());
      auto saved = files.read(path, codec);
      QVERIFY(saved.contents);
      QCOMPARE(saved.contents->source, expected + "x");
      QCOMPARE(saved.contents->format, (FileFormat{codec, withBom}));
      QVERIFY(!session.saveAs(path, FileFormat{Encoding::kUtf8, false}));
      QVERIFY(file.open(QIODevice::ReadOnly));
      QCOMPARE(file.readAll().toStdString(), expected + "x");
      file.close();
      QVERIFY(!files.write(path, {"", {codec, withBom}}));
      auto empty = files.read(path, codec);
      QVERIFY(empty.contents);
      QCOMPARE(empty.contents->source, std::string{});
      QCOMPARE(empty.contents->format, (FileFormat{codec, withBom}));
      QVERIFY(file.open(QIODevice::ReadOnly));
      QCOMPARE(file.readAll(), withBom ? bom : QByteArray());
    }
  }

  void realFileFailuresPreserveState() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QtDocumentFileService files;
    const auto name = dir.filePath("saved.md");
    const auto path = nativePath(name);
    DocumentSession session(files);
    QVERIFY(!session.replaceSource(session.snapshot().token, "original\r\n"));
    QVERIFY(!session.saveAs(path));
    const auto checkpoint = session.snapshot();
    QVERIFY(!session.save());
    QCOMPARE(session.snapshot().token, checkpoint.token);
    QVERIFY(!session.replaceSource(checkpoint.token, "dirty\n"));
    const auto before = session.snapshot();
    QVERIFY(session.saveAs(nativePath(dir.filePath("missing/new.md")),
                           FileFormat{Encoding::kUtf32Be, true}));
    QVERIFY(session.saveAs(nativePath(dir.path())));
    QCOMPARE(session.path(), path);
    QCOMPARE(session.snapshot().source, before.source);
    QCOMPARE(session.snapshot().format, before.format);
    QCOMPARE(session.snapshot().token, before.token);
    QVERIFY(session.dirty());
    QFile original(name);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), QByteArray("original\r\n"));
    original.close();
    const auto invalidName = dir.filePath("invalid.md");
    QFile invalid(invalidName);
    QVERIFY(invalid.open(QIODevice::WriteOnly));
    QCOMPARE(invalid.write(QByteArray::fromHex("efbbbf80")), qint64{4});
    invalid.close();
    QVERIFY(session.open(nativePath(invalidName), std::nullopt, true));
    QCOMPARE(session.snapshot().token, before.token);
    QCOMPARE(session.snapshot().source, before.source);
    QCOMPARE(session.path(), path);
    QVERIFY(session.dirty());
    QVERIFY(!session.replaceSource(before.token, checkpoint.source));
    QVERIFY(!session.dirty());
  }

  void malformedEncodings_data() {
    QTest::addColumn<int>("encoding");
    QTest::addColumn<QByteArray>("bytes");
    QTest::newRow("utf8-truncated")
        << int(Encoding::kUtf8) << QByteArray::fromHex("f09f98");
    QTest::newRow("utf16le-truncated")
        << int(Encoding::kUtf16Le) << QByteArray::fromHex("41");
    QTest::newRow("utf16be-high")
        << int(Encoding::kUtf16Be) << QByteArray::fromHex("d800");
    QTest::newRow("utf16le-low")
        << int(Encoding::kUtf16Le) << QByteArray::fromHex("00dc");
    QTest::newRow("utf32le-truncated")
        << int(Encoding::kUtf32Le) << QByteArray::fromHex("410000");
    QTest::newRow("utf32be-too-large")
        << int(Encoding::kUtf32Be) << QByteArray::fromHex("00110000");
    QTest::newRow("utf32le-surrogate")
        << int(Encoding::kUtf32Le) << QByteArray::fromHex("00d80000");
    QTest::newRow("utf32be-nul")
        << int(Encoding::kUtf32Be) << QByteArray::fromHex("00000000");
  }

  void malformedEncodings() {
    QFETCH(int, encoding);
    QFETCH(QByteArray, bytes);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto name = dir.filePath("invalid.md");
    QFile file(name);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();
    QtDocumentFileService files;
    const auto result =
        files.read(nativePath(name), static_cast<Encoding>(encoding));
    QVERIFY(!result.contents);
    QVERIFY(result.error);
    QCOMPARE(result.error->code, DocumentErrorCode::kEncoding);
    QVERIFY(!result.error->message.empty());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), bytes);
  }

  void detectionAndFailures() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto name = dir.filePath("input.md");
    const auto path = nativePath(name);
    QtDocumentFileService files;
    const auto put = [&](const QByteArray& bytes) {
      QFile file(name);
      return file.open(QIODevice::WriteOnly) &&
             file.write(bytes) == bytes.size();
    };
    for (const auto& entry :
         {std::pair{"41004200", Encoding::kUtf16Le},
          std::pair{"00410042", Encoding::kUtf16Be},
          std::pair{"4100000042000000", Encoding::kUtf32Le},
          std::pair{"0000004100000042", Encoding::kUtf32Be}}) {
      QVERIFY(put(QByteArray::fromHex(entry.first)));
      auto result = files.read(path);
      QVERIFY2(result.contents,
               result.error ? result.error->message.c_str() : "");
      QCOMPARE(result.contents->source, std::string("AB"));
      QCOMPARE(result.contents->format, (FileFormat{entry.second, false}));
    }
    for (const auto hex : {"ff", "f09f98", "fffe00d8", "0000feff00110000",
                           "efbbbf00", "fffe000000d80000"}) {
      QVERIFY(put(QByteArray::fromHex(hex)));
      const auto result = files.read(path);
      QVERIFY2(!result.contents, hex);
      QVERIFY(result.error);
      QVERIFY(!result.error->message.empty());
    }
    QVERIFY(put(QByteArray::fromHex("9f04a004")));
    QVERIFY(!files.read(path).contents);
    auto explicitRead = files.read(path, Encoding::kUtf16Le);
    QVERIFY(explicitRead.contents);
    QCOMPARE(explicitRead.contents->source,
             QByteArray::fromHex("d29fd2a0").toStdString());
    QVERIFY(put(QByteArray::fromHex("fffe4100")));
    QVERIFY(!files.read(path, Encoding::kUtf8).contents);
    QVERIFY(put(QByteArray::fromHex("efbbbfefbbbf41")));
    auto doubleBom = files.read(path);
    QVERIFY(doubleBom.contents);
    QCOMPARE(doubleBom.contents->source,
             QByteArray::fromHex("efbbbf41").toStdString());
    QVERIFY(!files.write(path, *doubleBom.contents));
    QFile preserved(name);
    QVERIFY(preserved.open(QIODevice::ReadOnly));
    QCOMPARE(preserved.readAll(), QByteArray::fromHex("efbbbfefbbbf41"));
    preserved.close();
    QVERIFY(put({}));
    auto empty = files.read(path);
    QVERIFY(empty.contents);
    QCOMPARE(empty.contents->source, std::string{});
    QVERIFY(!files.read(nativePath(dir.filePath("missing.md"))).contents);
    QVERIFY(!files.read({}).contents);
    QVERIFY(files.write({}, {"valid", {}}));
    QVERIFY(files.write(nativePath(dir.path()), {"valid", {}}));
    QVERIFY(files.write(nativePath(dir.filePath("missing/child.md")),
                        {"valid", {}}));
    QVERIFY(files.write(path, {"valid", {static_cast<Encoding>(99), false}}));
    QVERIFY(files.write(path, {std::string("\0", 1), {}}));
    QVERIFY(
        files.write(path, {QByteArray::fromHex("efbbbf41").toStdString(), {}}));
    QVERIFY(!files.read(path, static_cast<Encoding>(99)).contents);
  }
};

}  // namespace markpad

QTEST_GUILESS_MAIN(markpad::DocumentSessionTest)

#include "document_session_test.moc"
