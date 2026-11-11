# Task 034 — Table structure commands в continuous WYSIWYG

## Статус и владелец

- Статус: `Done`.
- Владелец: архитектор + C++ разработчик.
- Родитель: [Task 005](task_005.md). Зависимости: 021–024, 033.

## Контекст

Task 033 уже рендерит top-level table как QTextTable и позволяет править
существующий normal cell text. Engine Tasks 021–024 уже содержат проверенные
команды `InsertTableRow`, `RemoveTableRow`, `SetTableAlignment`,
`InsertTableColumn`, `RemoveTableColumn` и `RemoveTable`, но continuous UI не
имеет к ним доступа. Пользователь не может изменить структуру таблицы без
перехода в Markdown.

## Цель

Дать контекстные table actions в continuous WYSIWYG: вставка/удаление строк и
колонок, выравнивание колонки и удаление таблицы — через существующие
source-preserving engine commands, с сохранением Markdown, definitions,
delimiter row и общей истории.

## Область работ

**Включено:**

- `ContinuousEditor` mapping QTextTable → markdown table node и текущей ячейки
  (row/column/header/body).
- Контекстное меню continuous editor с действиями:
  - Insert Row Above / Below;
  - Delete Row;
  - Insert Column Left / Right;
  - Delete Column;
  - Align Column Left / Center / Right;
  - Delete Table.
- Валидация контекста: header row не удаляется и не получает row-above;
  последняя колонка не удаляется; actions disabled вне table и для
  неподдержанных/nested table.
- Payload для новых rows/columns — пустые Markdown cells; alignment новых
  колонок `kNone`; ordered/definitions/bystanders не меняются.
- `refreshState` обновляет enabled state контекстного table submenu.
- GUI tests для insert/remove row, insert/remove column, alignment, delete
  table, header/last-column guards, undo и source preservation.

**Не включено:**

- Создание таблицы из Format > Table.
- Empty-cell insertion/content editing.
- Merged cells, column resize, borders/themes, nested tables.
- Новые engine команды или изменение table serializer.
- Иконки для table context actions (не toolbar).
- Изменение меню/порядка, заданного sketch.md.

## Ограничения и предположения

- Table actions доступны через контекстное меню continuous editor, а не через
  Format menu/toolbar; sketch Format.Table остаётся disabled до creation slice.
- Current cell определяется `QTextCursor::currentTable()` и cell row/column;
  selection внутри одной ячейки не блокирует structural command, selection вне
  table контекст не даёт.
- Header row: row index 0; body rows начинаются с index 1. Insert row above
  header запрещён; insert below header создаёт первую body row.
- Новые cells создаются пустыми; engine сам сериализует их в `|  |`.
- Structural command валидируется engine AST guard; при reject Session не
  меняется, projection восстанавливается.
- Nested tables остаются placeholder и не получают table context actions.
- Undo/redo/dirty/token/mode switch — общий Session contract.

## Критерии приёмки

1. В continuous editor контекстное меню на table cell показывает Table
   submenu; вне table/nested table все table actions disabled.
2. Insert Row Above/Below создаёт корректную пустую row в header/body
   соответствии с engine contract; Delete Row удаляет body row и disabled для
   header.
3. Insert Column Left/Right создаёт пустую колонку с пустыми body cells и
   alignment `kNone`; Delete Column disabled для последней колонки.
4. Align Column Left/Center/Right меняет только delimiter row/alignment,
   сохраняя cell content и definitions.
5. Delete Table удаляет top-level table без потери окружающих блоков; nested
   table action недоступен.
6. Undo/redo, dirty/token и mode switch сохраняют общий Session contract;
   rejected command не меняет source.
7. GUI/engine tests покрывают success/rejection; Windows/Linux CTest и Linux
   sanitizers проходят.

## План проверок

- GUI fixtures: контекстное меню/action enabled state, insert/remove row,
  insert/remove column, three alignments, delete table, header/last-column
  guards, undo, mode switch.
- Engine regression: existing table command tests остаются без изменений.
- Windows UCRT64 Debug: build, CTest, MD4C spike.
- Ubuntu WSL: normal и ASan/UBSan/leaks CTest.
- `git diff --check --ignore-space-at-eol`.
- Native visual QA, clang-format и remote CI отдельно; если недоступны,
  указать явно.

## Зависимости и открытые вопросы

- Зависимости: table rendering Task 033; engine 021–024.
- Открытых блокирующих вопросов нет. Table creation и empty-cell editing
  отложены.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.

## Исполнительский промпт

Реализуй только table structure actions через контекстное меню continuous
editor. Используй существующие table engine commands и source maps; не
добавляй новые команды, creation flows, empty-cell editing или изменения
Format-меню/toolbar. Маппь QTextTable обратно к markdown node, определяй
текущую row/column, валидируй header/last-column guards и передавай commands
через общий `commandRequested`/Session path. Добавь GUI tests и отчёт здесь.

## Итог выполнения

### Реализация

- QTextTable mapping добавлен в `ContinuousEditor`: текущая table node и
  row/column/rows/columns берутся через `QTextCursor::currentTable()` и
  `QTextTable::cellAt`.
- Table context submenu добавлен только в context menu continuous editor:
  Insert Row Above/Below, Delete Row, Insert Column Left/Right, Delete Column,
  Align Column Left/Center/Right, Delete Table.
- `tableActionEnabled` контекстно отключает действия вне table, для header
  row-above/delete-row и для последней колонки; `refreshState` обновляет
  enabled state submenu.
- Действия используют существующие `InsertTableRow`, `RemoveTableRow`,
  `InsertTableColumn`, `RemoveTableColumn`, `SetTableAlignment` и
  `RemoveTable`; новые rows/columns создаются с пустыми cells, alignment новых
  колонок `kNone`.
- Все изменения идут через общий `commandRequested`/Session/history path;
  rejected command не меняет source, undo/redo и mode switch сохраняют
  contract. Nested/unsupported tables не получают table context.
- Добавлены GUI tests для insert/remove row, insert/remove column, трёх
  alignments, delete table, header/last-column guards, undo, context-action
  enabled state и single-column table.

### Проверки

- Windows UCRT64 Debug: build, **CTest 7/7 passed**; MD4C Qt Test **266
  passed**; GUI smoke **81 passed**.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build with the MD4C spike,
  CTest **7/7 passed**.
- Ubuntu sanitized Debug: ASan/UBSan/leaks with the MD4C spike, CTest **7/7
  passed**.
- `git diff --check --ignore-space-at-eol` прошёл. Native visual QA,
  clang-format, clang-tidy и remote CI не запускались.

### Ограничения

- Table structure commands доступны только через context menu continuous
  editor; Format > Table/toolbar table creation остаётся disabled.
- Empty-cell content insertion, table creation, merged cells, column resize,
  borders/themes и nested tables не добавлены.
- Table context actions не имеют иконок, потому что они не входят в toolbar
  sketch contract.
- Выбор текущей ячейки основан на позиции курсора; selection через несколько
  ячеек не даёт multi-cell structural command.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.
- Создан локальный коммит; push не выполнялся.
