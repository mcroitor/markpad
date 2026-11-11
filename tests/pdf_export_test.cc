#include <QFile>
#include <QFileInfo>
#include <QPrinter>
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
  }
};

}  // namespace markpad

QTEST_MAIN(markpad::PdfExportTest)

#include "pdf_export_test.moc"
