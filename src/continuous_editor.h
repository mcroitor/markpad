#ifndef MARKPAD_SRC_CONTINUOUS_EDITOR_H_
#define MARKPAD_SRC_CONTINUOUS_EDITOR_H_

#include <QWidget>
#include <optional>
#include <vector>

#include "document_session.h"
#include "markdown_document.h"
#include "settings.h"
#include "page_text_edit.h"

class QLabel;
class QTextEdit;
class QTextCursor;
class QTextCharFormat;

namespace markpad {

class PageTextEdit;

class ContinuousEditor final : public QWidget {
  Q_OBJECT

 public:
  explicit ContinuousEditor(QWidget* parent = nullptr);
  QTextEdit* editor() const { return editor_; }
  QString render(const DocumentSnapshot& snapshot);
  const DocumentToken& token() const { return token_; }
  markdown::ParseResult apply(const markdown::EditCommand& command) const;
  std::optional<qsizetype> sourcePosition(int position,
                                          bool forward = true) const;
  int viewPosition(qsizetype source, bool* exact) const;
  std::optional<size_t> headingNode() const;
  std::optional<size_t> taskNode() const;
  bool taskChecked() const;
  struct InlineState {
    std::optional<markdown::ToggleInlineStyle> command;
    bool checked = false;
    QString reason;
  };
  InlineState inlineState(markdown::InlineStyle style) const;
  void toggleInlineStyle(markdown::InlineStyle style);
  struct StructuralState {
    std::optional<markdown::EditCommand> command;
    bool checked = false;
    QString reason;
  };
  StructuralState blockquoteState() const;
  StructuralState listState(bool ordered) const;
  void toggleBlockquote();
  void changeListKind(bool ordered);
  void explainCursorMapping(bool exact);
  void setPaged(bool paged);
  bool paged() const { return paged_; }
  void setPageSetup(const PageSetup& setup);
  PageSetup pageSetup() const { return pageSetup_; }
  int pageCount() const;
  int currentPage() const;
  QString layoutWarning() const;

 signals:
  void commandRequested(const markpad::DocumentToken& token,
                        const markpad::markdown::EditCommand& command,
                        int cursor);
  void errorOccurred(const QString& message);

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  struct Run {
    size_t node;
    int start;
    int length;
  };
  struct CodeRun {
    size_t node;
    int start;
    int length;
  };
  struct Context {
    int start;
    int end;
    std::optional<size_t> heading;
    std::optional<size_t> task;
    size_t block;
    std::optional<size_t> list;
    std::optional<size_t> quote;
  };
  struct Change {
    int position;
    int removed;
    int added;
  };
  void renderBlock(size_t node, QTextCursor& cursor, int depth = 0,
                   QString prefix = {}, std::optional<size_t> task = {},
                   std::optional<size_t> list = {},
                   std::optional<size_t> quote = {});
  void renderCodeBlock(size_t node, QTextCursor& cursor, int depth,
                       std::optional<size_t> list,
                       std::optional<size_t> quote);
  void renderTable(size_t node, QTextCursor& cursor, int depth);
  void renderInline(size_t node, QTextCursor& cursor, QTextCharFormat format,
                    bool allowEditing = true);
  void beginBlock(QTextCursor& cursor, int depth);
  void opaque(QTextCursor& cursor, const QString& label, int depth,
              std::optional<size_t> block = {},
              std::optional<size_t> list = {},
              std::optional<size_t> quote = {});
  void changed();
  void restore(const QString& reason);
  const Context* context() const;

  QLabel* notice_;
  PageTextEdit* editor_;
  std::optional<markdown::Document> document_;
  DocumentToken token_;
  std::vector<Run> runs_;
  std::vector<CodeRun> codeRuns_;
  std::vector<Context> contexts_;
  std::vector<bool> mappedBlocks_;
  std::optional<Change> change_;
  QString renderedText_;
  bool rendering_ = false;
  bool firstBlock_ = true;
  bool paged_ = false;
  PageSetup pageSetup_;
};

}  // namespace markpad

#endif  // MARKPAD_SRC_CONTINUOUS_EDITOR_H_
