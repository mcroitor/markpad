# Task 040 — Выбор размера таблицы при создании

## Статус и владелец

- Статус: `Done`.
- Владелец: архитектор + C++ разработчик.
- Родитель: [Task 005](task_005.md). Зависимости: 036.

## Контекст

Task 036 включила Format > Table/toolbar Table, но всегда создаёт 2 columns ×
1 body row. Engine `InsertTable` уже принимает columns/bodyRows, поэтому
фиксированный default ограничивает пользователя без необходимости.

## Цель

Добавить безопасный size prompt перед созданием table: columns и body rows
выбираются в диалоге, engine получает выбранные размеры, а отмена диалога не
меняет документ/history.

## Область работ

**Включено:**

- `DocumentDialogs::chooseTableSize` и `TableSelection { columns, bodyRows }`.
- QDialog с QSpinBox constraints: columns 1–50, body rows 0–100.
- MainWindow Table action сначала запрашивает размер, затем вызывает
  `ContinuousEditor::insertTable(columns, bodyRows)`.
- Engine `InsertTable` использует выбранные размеры; текущий top-level
  insertion/blank-line/source-preservation contract сохраняется.
- TestDialogs override с default 2×1 для существующих тестов.
- GUI tests для custom size, zero body rows, cancel, bounds/disabled state,
  undo/mode switch.

**Не включено:**

- Drag-select grid size picker или live preview.
- Table styling/column widths/merges.
- Markdown-mode table creation.
- Новые engine команды или изменение table serializer.
- Image/link title prompts.

## Ограничения и предположения

- Default диалога 2 columns × 1 body row.
- Columns 1–50, body rows 0–100; значения вне диапазона clamp/disable.
- Cancel возвращает nullopt и не вызывает command.
- Header cells остаются `Column N`; body cells пустые.
- Engine guard остаётся последней защитой от invalid size.

## Критерии приёмки

1. Table action открывает size dialog; OK с 3×2 создаёт 3-column table с
   header + 2 body rows.
2. Body rows 0 создаёт header-only table.
3. Cancel не меняет source/history/dirty.
4. Bounds columns/body rows ограничены dialog constraints; engine reject
   сохраняется для некорректных значений.
5. Существующий default Table action test продолжает работать через default
   TestDialogs 2×1.
6. Undo/redo, mode switch и source preservation сохраняются.
7. Windows/Linux CTest и Linux sanitizers проходят.

## План проверок

- GUI fixtures: custom 3×2, header-only, cancel, default 2×1, undo, mode switch.
- Engine regression: existing InsertTable fixtures без изменений.
- Windows UCRT64 Debug: build, CTest, MD4C spike.
- Ubuntu WSL: normal и ASan/UBSan/leaks CTest.
- `git diff --check --ignore-space-at-eol`.
- Native visual QA, clang-format и remote CI отдельно; если недоступны,
  указать явно.

## Зависимости и открытые вопросы

- Зависимости: Task 036 table creation.
- Открытых блокирующих вопросов нет.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.

## Исполнительский промпт

Реализуй только size prompt для существующего Table action. Добавь
`chooseTableSize`/`TableSelection`, обнови MainWindow и
`ContinuousEditor::insertTable(columns, bodyRows)`, сохрани existing engine
contract. Не добавляй grid picker, styling, Markdown-mode creation или новые
команды. Cancel не должен менять Session. Добавь GUI tests и отчёт здесь.

## Итог выполнения

### Реализация

- Добавлены `TableSelection` и `DocumentDialogs::chooseTableSize` с QDialog
  QSpinBox constraints columns 1–50, body rows 0–100.
- MainWindow Table action сначала запрашивает размер; Cancel не вызывает
  command. `ContinuousEditor::insertTable(columns, bodyRows)` передаёт размеры
  существующему `InsertTable` engine command.
- TestDialogs default 2×1 сохраняет прежние tests; добавлены custom 3×2,
  header-only, cancel и default checks.
- Insertion/blank-line/EOL/source-preservation contract Task 036 не менялся.

### Проверки

- Windows UCRT64 Debug: build, **CTest 7/7 passed**; MD4C Qt Test **313
  passed**; GUI smoke **86 passed**.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build with the MD4C spike,
  CTest **7/7 passed**.
- Ubuntu sanitized Debug: ASan/UBSan/leaks with the MD4C spike, CTest **7/7
  passed**.
- `git diff --check --ignore-space-at-eol` прошёл. Native visual QA,
  clang-format, clang-tidy и remote CI не запускались.

### Ограничения

- Size dialog использует spin boxes, без drag-select grid или preview.
- Columns/body rows ограничены 1–50 / 0–100; engine guard остаётся
  дополнительной проверкой.
- Table styling, widths, merges и Markdown-mode creation не добавлены.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.
- Создан локальный коммит; push не выполнялся.
