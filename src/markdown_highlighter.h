#ifndef MARKPAD_SRC_MARKDOWN_HIGHLIGHTER_H_
#define MARKPAD_SRC_MARKDOWN_HIGHLIGHTER_H_

#include <QSyntaxHighlighter>

namespace markpad {

class MarkdownHighlighter final : public QSyntaxHighlighter {
 public:
  explicit MarkdownHighlighter(QTextDocument* document);

 protected:
  void highlightBlock(const QString& text) override;
};

}  // namespace markpad

#endif  // MARKPAD_SRC_MARKDOWN_HIGHLIGHTER_H_
