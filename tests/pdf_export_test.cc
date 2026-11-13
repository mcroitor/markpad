#include <QFile>
#include <QFileInfo>
#include <QPrinter>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "markdown_document.h"
#include "pdf_export.h"

namespace markpad {
namespace {

std::filesystem::path nativePath(const QString& path) {
#ifdef _WIN32
  return std::filesystem::path(path.toStdWString());
#else
  return std::filesystem::path(path.toStdString());
#endif
}

PdfExportOptions options() {
  PdfExportOptions value;
  value.page.size = PageSize::kA4;
  value.page.orientation = PageOrientation::kPortrait;
  value.page.marginTopMm = 20.0;
  value.page.marginRightMm = 20.0;
  value.page.marginBottomMm = 20.0;
  value.page.marginLeftMm = 20.0;
  value.fontFamily = QStringLiteral("DejaVu Sans");
  value.fontSize = 11;
  return value;
}

QByteArray markdownFixture() {
  QByteArray source;
  source += "# Title\n\nA **bold** paragraph.\n\n";
  source += "> quote\n\n- one\n- two\n\n";
  source += "```txt\nx < y\n```\n\n";
  source += "| h | i |\n| :---: | ---: |\n| a | b |\n\n";
  for (int i = 0; i < 60; ++i) {
    source += "Paragraph " + QByteArray::number(i) + "\n\n";
  }
  return source;
}

int pdfPageCount(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return -1;
  }
  const auto data = file.readAll();
  return data.count("/Type /Page") - data.count("/Type /Pages");
}

QSizeF pdfMediaBox(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  const auto data = QString::fromLatin1(file.readAll());
  const QRegularExpression expression(
      QStringLiteral("/MediaBox\\s*\\[\\s*0\\s+0\\s+([0-9.]+)\\s+([0-9.]+)\\s*\\]"));
  const auto match = expression.match(data);
  if (!match.hasMatch()) {
    return {};
  }
  return QSizeF(match.captured(1).toDouble(), match.captured(2).toDouble());
}

}  // namespace

class PdfExportTest : public QObject {
  Q_OBJECT

 private slots:
  void exportsSafePdf() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto parsed = markdown::parse(markdownFixture());
    QVERIFY(parsed.document);
    const auto path = directory.filePath(QStringLiteral("document.pdf"));
    const auto result =
        exportPdf(*parsed.document, nativePath(path), options());
    QVERIFY2(result.error.value_or(QString()).isEmpty(),
             qPrintable(result.error.value_or(QString{})));
    QVERIFY(result.pageCount >= 1);
    const QFileInfo info(path);
    QVERIFY(info.exists());
    QVERIFY(info.size() > 0);
    QCOMPARE(pdfPageCount(path), result.pageCount);
    const auto media = pdfMediaBox(path);
    QVERIFY(qAbs(media.width() - 595.0) < 2.0);
    QVERIFY(qAbs(media.height() - 842.0) < 2.0);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.read(4) == "%PDF");
  }

  void pageSizesAndMarginsAreApplied() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto parsed = markdown::parse(markdownFixture());
    QVERIFY(parsed.document);
    auto letter = options();
    letter.page.size = PageSize::kLetter;
    letter.page.marginTopMm = 45.0;
    letter.page.marginBottomMm = 45.0;
    const auto path = directory.filePath(QStringLiteral("letter.pdf"));
    const auto result =
        exportPdf(*parsed.document, nativePath(path), letter);
    QVERIFY2(result.success(), qPrintable(result.error.value_or(QString{})));
    QVERIFY(QFileInfo::exists(path));
    QCOMPARE(pdfPageCount(path), result.pageCount);
    const auto media = pdfMediaBox(path);
    QVERIFY(qAbs(media.width() - 612.0) < 2.0);
    QVERIFY(qAbs(media.height() - 792.0) < 2.0);
  }

  void overflowRefusedWithoutDamagingExistingFile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QByteArray source;
    source += "word ";
    source = source.repeated(20000);
    source += "\n";
    const auto parsed = markdown::parse(source);
    QVERIFY(parsed.document);
    const auto path = directory.filePath(QStringLiteral("existing.pdf"));
    {
      QFile file(path);
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write("keep");
    }
    const auto result =
        exportPdf(*parsed.document, nativePath(path), options());
    QVERIFY(!result.success());
    QVERIFY(result.overflow);
    QVERIFY(!result.error->isEmpty());
    QVERIFY(result.error->contains(QStringLiteral("refused")));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("keep"));
  }

  void printRefusesOversizedBlock() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QByteArray source;
    source = QByteArray("word ").repeated(20000);
    source += "\n";
    const auto parsed = markdown::parse(source);
    QVERIFY(parsed.document);
    const auto path = directory.filePath(QStringLiteral("print-overflow.pdf"));
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    const auto error = printDocument(*parsed.document, printer, options());
    QVERIFY(error.has_value());
    QVERIFY(error->contains(QStringLiteral("refused")));
  }

  void reportsWriteErrors() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto parsed = markdown::parse("# h\n\nbody\n");
    QVERIFY(parsed.document);
    const auto directoryResult = exportPdf(
        *parsed.document, nativePath(directory.path()), options());
    QVERIFY(!directoryResult.success());
    QVERIFY(!directoryResult.error->isEmpty());
    const auto emptyResult =
        exportPdf(*parsed.document, std::filesystem::path{}, options());
    QVERIFY(!emptyResult.success());
  }

  void printHelperProducesPdf() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto parsed = markdown::parse(markdownFixture());
    QVERIFY(parsed.document);
    const auto path = directory.filePath(QStringLiteral("print.pdf"));
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    const auto error = printDocument(*parsed.document, printer, options());
    QVERIFY2(!error, qPrintable(error.value_or(QString{})));
    QVERIFY(QFileInfo::exists(path));
    QVERIFY(QFileInfo(path).size() > 0);
    QVERIFY(pdfPageCount(path) >= 1);
  }
};

}  // namespace markpad

QTEST_MAIN(markpad::PdfExportTest)

#include "pdf_export_test.moc"
