#include "markdown_highlighter.h"

#include <QRegularExpression>
#include <QTextCharFormat>

namespace markpad {

MarkdownHighlighter::MarkdownHighlighter(QTextDocument* document)
    : QSyntaxHighlighter(document) {}

void MarkdownHighlighter::highlightBlock(const QString& text) {
  QTextCharFormat code;
  code.setForeground(QColor(110, 80, 40));
  static const QRegularExpression fence(
      QStringLiteral("^ {0,3}(`{3,}|~{3,})(.*)$"));
  const auto match = fence.match(text);
  const int previous = previousBlockState();
  setCurrentBlockState(0);
  if (previous >= 6) {
    setFormat(0, text.size(), code);
    const bool tilde = (previous % 2) != 0;
    const bool closes =
        match.hasMatch() &&
        (match.captured(1).front() == QChar(tilde ? '~' : '`')) &&
        match.capturedLength(1) >= previous / 2 &&
        match.captured(2).trimmed().isEmpty();
    if (!closes) {
      setCurrentBlockState(previous);
    }
    return;
  }
  if (match.hasMatch() &&
      (match.captured(1).front() != '`' || !match.captured(2).contains('`'))) {
    setCurrentBlockState(int(match.capturedLength(1)) * 2 +
                         (match.captured(1).front() == '~' ? 1 : 0));
    setFormat(0, text.size(), code);
    return;
  }
  QTextCharFormat heading;
  heading.setForeground(QColor(35, 80, 170));
  heading.setFontWeight(QFont::Bold);
  static const QRegularExpression headingPattern(
      QStringLiteral("^ {0,3}#{1,6}(?:\\s|$).*"));
  if (headingPattern.match(text).hasMatch()) {
    setFormat(0, text.size(), heading);
  }
  QTextCharFormat marker;
  marker.setForeground(QColor(100, 60, 150));
  static const QRegularExpression markers(
      QStringLiteral("^\\s*(?:>+|[-+*]\\s|\\d{1,9}[.)]\\s)"));
  const auto prefix = markers.match(text);
  if (prefix.hasMatch()) {
    setFormat(prefix.capturedStart(), prefix.capturedLength(), marker);
  }
  const auto apply = [&](const QRegularExpression& pattern,
                         const QTextCharFormat& format) {
    auto matches = pattern.globalMatch(text);
    while (matches.hasNext()) {
      const auto value = matches.next();
      setFormat(value.capturedStart(), value.capturedLength(), format);
    }
  };
  QTextCharFormat emphasis;
  emphasis.setFontItalic(true);
  static const QRegularExpression emphasisPattern(
      QStringLiteral("(?:\\*[^*\\n]+\\*|_[^_\\n]+_)"));
  apply(emphasisPattern, emphasis);
  QTextCharFormat link;
  link.setForeground(QColor(25, 120, 120));
  static const QRegularExpression links(
      QStringLiteral("!?\\[[^\\]\\n]*\\]\\([^\\)\\n]*\\)"));
  apply(links, link);
  static const QRegularExpression inlineCode(QStringLiteral("`[^`\\n]+`"));
  apply(inlineCode, code);
}

}  // namespace markpad
