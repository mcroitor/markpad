# Task 036 — Создание таблицы из Format > Table

## Статус и владелец

- Статус: `Done`.
- Владелец: архитектор + C++ разработчик.
- Родитель: [Task 005](task_005.md). Зависимости: 021–024, 033–035.

## Контекст

После Tasks 033–035 top-level tables рендерятся, редактируются, получают
structure actions и допускают ввод в пустые cells. Но создать новую таблицу
из UI всё ещё нельзя: Format > Table и toolbar Table остаются disabled.
Пользователь вынужден вручную набирать Markdown table в source mode.

## Цель

Включить существующее действие Format > Table/toolbar Table в continuous
WYSIWYG и вставлять source-preserving GFM table после текущего top-level блока
(или в конец документа), используя новый engine command и общий
Session/history contract. Default table имеет заголовки `Column 1..N` и пустые
body cells, потому что MD4C не даёт надёжный `mapTable` для полностью пустой
header row.

## Область работ

**Включено:**

- Новый engine command `InsertTable`:
  - top-level table с заданными columns/bodyRows;
  - default 2 columns × 1 body row (header + one body row);
  - header cells `Column 1..N` для совместимости с `mapTable`; body cells пустые;
  - EOL style берётся из документа;
  - вставка после текущего top-level block или в конец документа;
  - корректные blank-line separators до/после таблицы;
  - candidate table parse, graft в expected AST и полный `apply` guard;
  - source bytes вне insertion range не меняются.
- Continuous UI:
  - `ContinuousEditor::insertTable()` и enabled state;
  - позиция вставки после top-level block под курсором, после таблицы или в
    конец документа;
  - newly inserted table сразу рендерится и его empty cells редактируемы.
- Format menu/toolbar:
  - Table action становится активным в continuous mode;
  - object name/order/sketch menu structure не меняются.
- Engine/GUI tests для empty document, after paragraph, after table, CRLF,
  surrounding bytes, blank-line separation, undo/redo, mode switch и
  последующего заполнения cell.

**Не включено:**

- Table size picker/dialog, custom column/row count UI.
- Table creation в Markdown mode.
- Nested table creation, merged cells, column widths, borders/themes.
- Rich cell content, multiline cells, images/links/tables inside cells.
- Изменение table serializer или новых внешних зависимостей.

## Ограничения и предположения

- Default shape: 2 columns, header row `Column 1`/`Column 2` + 1 body row с
  пустыми cells.
- Insertion point — после текущего top-level block; если курсор в table cell,
  после этой таблицы; если нет контекста — в конец документа.
- Для разделения блоков используются blank lines с document EOL; существующие
  bytes вне insertion range не переписываются.
- Новые таблицы имеют outer pipes и canonical delimiter row, поэтому сразу
  поддерживают Tasks 033–035.
- Engine reject (invalid position/size, structural mismatch) не меняет Session.
- Table action доступен только в continuous WYSIWYG.

## Критерии приёмки

1. Format > Table/toolbar Table enabled в continuous mode и disabled в
   Markdown mode.
2. Вставка в пустой документ создаёт 2×1 table с `Column 1`/`Column 2`
   header, корректным delimiter/body и EOL документа.
3. Вставка после paragraph/table сохраняет соседние bytes и отделяет таблицу
   blank line; cursor/scroll не теряет документ.
4. CRLF документ получает CRLF table.
5. Undo/redo возвращает точный source; dirty/token/mode switch сохраняются.
6. Новая table сразу рендерится как QTextTable, и empty cells принимают текст
   через Task 035.
7. Engine/GUI tests покрывают success/rejection; Windows/Linux CTest и Linux
   sanitizers проходят.

## План проверок

- Engine fixtures: empty document, after paragraph, middle insertion, after
  table, CRLF, invalid size/position, no-op/stale token, surrounding bytes.
- GUI fixtures: Format/toolbar action enabled state, insertion, undo/mode
  switch, render + empty-cell typing.
- Windows UCRT64 Debug: build, CTest, MD4C spike.
- Ubuntu WSL: normal и ASan/UBSan/leaks CTest.
- `git diff --check --ignore-space-at-eol`.
- Native visual QA, clang-format и remote CI отдельно; если недоступны,
  указать явно.

## Зависимости и открытые вопросы

- Зависимости: table map/commands 021–024, rendering/editing 033–035.
- Открытых блокирующих вопросов нет. Size picker и nested tables отложены.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.

## Исполнительский промпт

Реализуй только создание default top-level table из Format > Table/toolbar.
Добавь engine `InsertTable` с fixtures, затем insertion point/action в
ContinuousEditor/MainWindow. Не добавляй size dialog, Markdown-mode creation,
nested tables или новые зависимости. Используй parse/graft/apply guard и
сохраняй bytes вне insertion range. Проверь Windows/Linux tests и sanitizers;
отчёт добавь сюда.

## Итог выполнения

### Реализация

- Добавлен `InsertTable` для top-level source-preserving вставки: default
  2 columns × 1 body row, `Column 1..N` header, пустые body cells, EOL из
  документа, blank-line separators до/после таблицы.
- Insertion point: после текущего top-level block, после текущей table или в
  конец документа. Candidate table парсится отдельно, graft-ится в expected
  AST, полный `apply` guard проверяет окружение.
- Continuous UI: `ContinuousEditor::insertTable()` и
  `tableCreationEnabled()`; Format > Table/toolbar Table активны в continuous
  mode и disabled в Markdown.
- Source bytes вне insertion range не меняются; CRLF сохраняется. Новая table
  сразу рендерится и её empty cells принимают текст через Task 035.
- Engine fixtures покрывают empty/after/before/middle/CRLF/header-only,
  invalid position/dimensions, created-table source map и stale token.
  GUI tests покрывают action enabled state, insertion after paragraph,
  empty-document insertion, render, cell typing, undo и mode switch.

### Проверки

- Windows UCRT64 Debug: build, **CTest 7/7 passed**; MD4C Qt Test **291
  passed**; GUI smoke **83 passed**.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build with the MD4C spike,
  CTest **7/7 passed**.
- Ubuntu sanitized Debug: ASan/UBSan/leaks with the MD4C spike, CTest **7/7
  passed**.
- `git diff --check --ignore-space-at-eol` прошёл. Native visual QA,
  clang-format, clang-tidy и remote CI не запускались.

### Ограничения

- Default shape фиксирован (2 columns, header + 1 body row); size picker/dialog
  не добавлен.
- Header cells содержат `Column N`, потому что полностью пустая header row не
  проходит `mapTable`; body cells пустые и редактируемые.
- Table creation доступен только в continuous mode; Markdown-mode insertion,
  nested tables, size picker и rich/multiline cells не включены.
- Insertion требует находимый boundary следующего block; для экзотических
  соседних block types возможен explicit reject.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.
- Создан локальный коммит; push не выполнялся.
