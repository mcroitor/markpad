# Task 035 — Ввод текста в пустые table cells в continuous WYSIWYG

## Статус и владелец

- Статус: `Done`.
- Владелец: архитектор + C++ разработчик.
- Родитель: [Task 005](task_005.md). Зависимости: 021–024, 033, 034.

## Контекст

Task 033/034 уже рисуют top-level tables и позволяют вставлять/удалять строки,
колонки и менять alignment. Но новая пустая cell не содержит normal text run,
поэтому `ContinuousEditor` не может ввести в неё текст: `changed()` не находит
editable target и восстанавливает projection. Пользователь вынужден
переключаться в Markdown, чтобы заполнить только что созданную ячейку.

## Цель

Разрешить ввод одной строки plain Markdown text в пустую top-level table cell
через source-preserving engine command, сохраняя table structure, delimiter
row, alignment, definitions и все байты вне cell range.

## Область работ

**Включено:**

- Новый engine command `InsertTableCellText { cell node, text }`:
  - допустим только для пустой `TH`/`TD` cell с точным `mapTable` range;
  - принимает nonempty single-line UTF-8 text без NUL;
  - использует существующий table-cell escaping для `|` и Markdown
    punctuation;
  - парсит candidate table в context, grafts table AST и применяет `apply`
    patch только к cell range;
  - сохраняет opening/closing pipes, spacing, delimiter row, alignment,
    definitions и соседние байты.
- Continuous `emptyCellRuns_` mapping:
  - пустая cell получает zero-length editable target в QTextTable cursor;
  - ввод/plain paste одной строки отправляет `InsertTableCellText`;
  - Enter/newline, invalid Unicode/NUL и nonempty cell остаются protected.
- GUI tests для header/body empty cell, escaping, undo/redo, mode switch,
  newly inserted row/column cell, rejection и source preservation.

**Не включено:**

- Создание таблицы из Format > Table.
- Редактирование nonempty cell через новый command (existing ReplaceText
  остаётся).
- Empty-cell multiline content, rich formatting, links, images или tables
  inside cells.
- Merged cells, column resize, borders/themes и nested tables.
- Новые dialogs, внешние зависимости или table serializer redesign.

## Ограничения и предположения

- Поддерживаются только top-level tables с успешным `mapTable`.
- Empty cell определяется отсутствием children у cell node; cell с
  protected-only content не считается empty.
- `InsertTableCellText` принимает одну строку; Markdown punctuation
  escape-ится, новые structural nodes не создаются.
- Cell range может быть zero-length; patch применяется к точному range, а не
  ко всей таблице.
- Опасная правка, меняющая table structure, отвергается engine AST guard;
  Session не меняется.
- Undo/redo/dirty/token/mode switch — общий Session contract.

## Критерии приёмки

1. Пустая header/body cell top-level table принимает plain text через
   continuous UI; source меняется только в этой cell.
2. `|`, `*`, `\` и другой Markdown punctuation корректно escape-ятся и не
   ломают таблицу.
3. Новая row/column (Task 034) сразу содержит editable empty cells.
4. Nonempty cell, nested table, newline/empty/invalid/NUL text и structural
   mismatch отвергаются без изменения source.
5. Delimiter row, alignment, definitions, соседние blocks и table spacing
   сохраняются.
6. Undo/redo, dirty/token, mode switch и rejected projection восстанавливаются.
7. Engine/GUI tests покрывают success/rejection; Windows/Linux CTest и Linux
   sanitizers проходят.

## План проверок

- Engine fixtures: header/body empty cell, pipe/star/backslash escaping,
  surrounding bytes, nonempty cell reject, nested table reject, newline/NUL/
  invalid UTF-8 reject, stale/no-op.
- GUI fixtures: empty cell typing, newly inserted row/column cell, undo, mode
  switch, rejection recovery.
- Windows UCRT64 Debug: build, CTest, MD4C spike.
- Ubuntu WSL: normal и ASan/UBSan/leaks CTest.
- `git diff --check --ignore-space-at-eol`.
- Native visual QA, clang-format и remote CI отдельно; если недоступны,
  указать явно.

## Зависимости и открытые вопросы

- Зависимости: table map/commands 021–024, rendering 033, structure actions 034.
- Открытых блокирующих вопросов нет. Table creation и multiline/rich cell
  content отложены.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.

## Исполнительский промпт

Реализуй только empty-cell plain-text insertion. Добавь `InsertTableCellText`
и fixtures, потом `emptyCellRuns_` в `ContinuousEditor` и обработку ввода.
Используй `mapTable`, `parseTableContext`/graft и общий `apply` AST guard; не
пересериализуй всю таблицу, не добавляй table creation, multiline/rich cell
editing или новые зависимости. Empty cell определяется отсутствием children.
При reject восстанавливай projection и не меняй Session. Проверь Windows/
Linux tests и sanitizers; отчёт добавь сюда.

## Итог выполнения

### Реализация

- Добавлен `InsertTableCellText` для top-level empty TH/TD: exact `mapTable`
  cell range, plain single-line UTF-8, `|`/Markdown punctuation escaping,
  `parseTableContext` + `graftList` + общий `apply` AST guard.
- Патч применяется к cell segment между outer pipes с нормализованными
  пробелами; delimiter row, alignment, definitions и соседние bytes не
  меняются.
- `ContinuousEditor` получил `emptyCellRuns_`: пустая cell отображается как
  zero-length editable target; ввод одной строки отправляет
  `InsertTableCellText`. Nonempty/protected cells и edge cases остаются
  protected.
- Engine fixtures покрывают header/body, pipe/markup escaping, outside bytes,
  CRLF, nonempty/nested/empty/newline/NUL/invalid/whitespace rejection и
  stale token.
- GUI tests покрывают empty body cell typing, pipe escaping, newly inserted
  column cell, undo и mode switch. Existing table actions/rendering regressions
  остаются зелёными.

### Проверки

- Windows UCRT64 Debug: build, **CTest 7/7 passed**; MD4C Qt Test **280
  passed**; GUI smoke **82 passed**.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build with the MD4C spike,
  CTest **7/7 passed**.
- Ubuntu sanitized Debug: ASan/UBSan/leaks with the MD4C spike, CTest **7/7
  passed**.
- `git diff --check --ignore-space-at-eol` прошёл. Native visual QA,
  clang-format, clang-tidy и remote CI не запускались.

### Ограничения

- Поддерживаются только top-level outer-piped tables; nested tables и rows без
  outer pipes не получают empty-cell command.
- Ввод ограничен одной строкой plain text; multiline/rich content, links,
  images и tables inside cells не добавлены.
- Nonempty cells продолжают использовать ReplaceText; новый command для них
  не применяется.
- Table creation, merged cells, column resize, borders/themes остаются вне
  среза.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.
- Создан локальный коммит; push не выполнялся.
