#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "html_export.h"
#include "qt_html_export_service.h"

namespace markpad {
namespace {

QString render(const QByteArray& source) {
  const auto parsed = markdown::parse(source);
  if (!parsed.document) {
    return {};
  }
  const auto result = markdown::renderHtml(*parsed.document);
  return result.html.value_or(QString{});
}

std::filesystem::path nativePath(const QString& path) {
#ifdef _WIN32
  return std::filesystem::path(path.toStdWString());
#else
  return std::filesystem::path(path.toStdString());
#endif
}

}  // namespace

class HtmlExportTest : public QObject {
  Q_OBJECT

 private slots:
  void rendersGfmStructure() {
    const QByteArray source =
        "# Title\n\n"
        "A **bold** *italic* ~~strike~~ `code` [link](https://example.test "
        "\"T\") ![alt](https://example.test/image.png \"I\").\n\n"
        "> quote\n\n"
        "- one\n- two\n\n"
        "- [ ] todo\n- [x] done\n\n"
        "1. first\n2. second\n\n"
        "```txt\nx < y\n```\n\n"
        "| h | i |\n| :---: | ---: |\n| a | b |\n";
    const auto html = render(source);
    QVERIFY(html.startsWith(QStringLiteral("<!DOCTYPE html>")));
    QVERIFY(html.contains(QStringLiteral("<h1>Title</h1>")));
    QVERIFY(html.contains(QStringLiteral("<strong>bold</strong>")));
    QVERIFY(html.contains(QStringLiteral("<em>italic</em>")));
    QVERIFY(html.contains(QStringLiteral("<del>strike</del>")));
    QVERIFY(html.contains(QStringLiteral("<code>code</code>")));
    QVERIFY(html.contains(
        QStringLiteral("<a href=\"https://example.test\" title=\"T\" "
                       "rel=\"noopener noreferrer\">link</a>")));
    QVERIFY(html.contains(QStringLiteral("<img src=\"https://example.test/image.png\"")));
    QVERIFY(html.contains(QStringLiteral("<blockquote>")));
    QVERIFY(html.contains(QStringLiteral("<ul>")));
    QVERIFY(html.contains(QStringLiteral("<ol>")));
    QVERIFY(html.contains(QStringLiteral("<input type=\"checkbox\" disabled>")));
    QVERIFY(html.contains(QStringLiteral("todo")));
    QVERIFY(html.contains(
        QStringLiteral("<input type=\"checkbox\" checked disabled>")));
    QVERIFY(html.contains(QStringLiteral("done")));
    QVERIFY(html.contains(QStringLiteral("<pre><code>x &lt; y\n</code></pre>")));
    QVERIFY(html.contains(QStringLiteral("<table>")));
    QVERIFY(html.contains(QStringLiteral("<th align=\"center\">h</th>")));
    QVERIFY(html.contains(QStringLiteral("<td align=\"right\">b</td>")));
    QVERIFY(html.contains(QStringLiteral("</html>")));
  }

  void escapesRawHtml() {
    const auto html = render(
        "<script>alert(1)</script>\n\n"
        "Inline <img src=x onerror=alert(1)> text.\n\n"
        "<div onclick=\"bad()\">raw</div>\n");
    QVERIFY(!html.contains(QStringLiteral("<script>")));
    QVERIFY(!html.contains(QStringLiteral("<img src=x")));
    QVERIFY(!html.contains(QStringLiteral("<div onclick")));
    QVERIFY(html.contains(QStringLiteral("&lt;script&gt;")));
    QVERIFY(html.contains(QStringLiteral("&lt;img src=x")));
    QVERIFY(html.contains(QStringLiteral("&lt;div onclick")));
  }

  void omitsUnsafeSchemes() {
    const auto source =
        "[script](javascript:alert(1)) [data](data:text/html,x) "
        "[file](file:///private) [vbs](vbscript:x)\n\n"
        "![img](javascript:alert(1)) ![data](data:image/png,x)\n\n"
        "[safe](https://example.test) [relative](/local)\n";
    const auto html = render(source);
    QVERIFY(!html.contains(QStringLiteral("href=\"javascript:")));
    QVERIFY(!html.contains(QStringLiteral("href=\"data:")));
    QVERIFY(!html.contains(QStringLiteral("href=\"file:")));
    QVERIFY(!html.contains(QStringLiteral("href=\"vbscript:")));
    QVERIFY(!html.contains(QStringLiteral("src=\"javascript:")));
    QVERIFY(!html.contains(QStringLiteral("src=\"data:")));
    QVERIFY(html.contains(QStringLiteral("href=\"https://example.test\"")));
    QVERIFY(html.contains(QStringLiteral("href=\"/local\"")));
    QVERIFY(html.contains(QStringLiteral("script")));
    QVERIFY(html.contains(QStringLiteral("img")));
  }

  void preservesUnicodeAndEntities() {
    const auto source =
        QStringLiteral("# \u0417\u0430\u0433\u043e\u043b\u043e\u0432\u043e\u043a\n\n"
                       "\u0442\u0435\u043a\u0441\u0442 \U0001f600 &amp; &lt;\n")
            .toUtf8();
    const auto html = render(source);
    QVERIFY(html.contains(QStringLiteral("<h1>\u0417\u0430\u0433\u043e\u043b\u043e\u0432\u043e\u043a</h1>")));
    QVERIFY(html.contains(QStringLiteral("\u0442\u0435\u043a\u0441\u0442 \U0001f600")));
    QVERIFY(html.contains(QStringLiteral("&amp;")));
    QVERIFY(html.contains(QStringLiteral("&lt;")));
  }

  void writesUtf8FileAndReportsErrors() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("document.html"));
    const auto html = render("# h\n\nbody\n");
    QVERIFY(!html.isEmpty());
    const auto error = writeHtmlFile(nativePath(path), html);
    QVERIFY2(!error, qPrintable(error.value_or(QString{})));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), html.toUtf8());
    file.close();

    const auto invalid = writeHtmlFile(std::filesystem::path{}, html);
    QVERIFY(invalid);
    const auto directoryPath =
        writeHtmlFile(nativePath(directory.path()), html);
    QVERIFY(directoryPath);
  }
};

}  // namespace markpad

QTEST_GUILESS_MAIN(markpad::HtmlExportTest)

#include "html_export_test.moc"
