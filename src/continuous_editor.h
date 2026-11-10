#ifndef MARKPAD_SRC_CONTINUOUS_EDITOR_H_
#define MARKPAD_SRC_CONTINUOUS_EDITOR_H_

#include <QWidget>
#include <optional>
#include <vector>

#include "document_session.h"
#include "markdown_document.h"

class QLabel;
class QTextEdit;
class QTextCursor;
class QTextCharFormat;

namespace markpad {

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
  void explainCursorMapping(bool exact);

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
  struct Context {
    int start;
    int end;
    std::optional<size_t> heading;
    std::optional<size_t> task;
  };
  struct Change {
    int position;
    int removed;
    int added;
  };
  void renderBlock(size_t node, QTextCursor& cursor, int depth = 0,
                   QString prefix = {}, std::optional<size_t> task = {});
  void renderInline(size_t node, QTextCursor& cursor, QTextCharFormat format,
                    bool allowEditing = true);
  void beginBlock(QTextCursor& cursor, int depth);
  void opaque(QTextCursor& cursor, const QString& label, int depth);
  void changed();
  void restore(const QString& reason);
  const Context* context() const;

  QLabel* notice_;
  QTextEdit* editor_;
  std::optional<markdown::Document> document_;
  DocumentToken token_;
  std::vector<Run> runs_;
  std::vector<Context> contexts_;
  std::vector<bool> mappedBlocks_;
  std::optional<Change> change_;
  QString renderedText_;
  bool rendering_ = false;
  bool firstBlock_ = true;
};

}  // namespace markpad

#endif  // MARKPAD_SRC_CONTINUOUS_EDITOR_H_
