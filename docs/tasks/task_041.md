# Task 041 — Inline code spans: rendering и локальное редактирование

## Статус и владелец

- Статус: `Done`.
- Владелец: архитектор + C++ разработчик.
- Родитель: [Task 005](task_005.md). Зависимости: 013, 026, 031.

## Контекст

Fenced code blocks уже отображаются и редактируются (Task 031), но inline
code span (`\`code\``) остаётся placeholder `[Read-only: code]`. Это часть GFM
code support из Task 005 и заметный разрыв между Markdown и WYSIWYG.

## Цель

Показывать inline code текстом в monospace-стиле и разрешить локальное
редактирование content внутри существующего код-спана, сохраняя backtick
delimiters и все байты вне content range.

## Область работ

**Включено:**

- Engine `ReplaceInlineCode { textNode, text }`:
  - text node type `MD_TEXT_CODE` с parent `MD_SPAN_CODE`;
  - exact source range content, nonempty single-line UTF-8 без NUL;
  - expected AST обновляет только text content, сохраняя delimiters;
  - full `apply` guard; delimiter collision/invalid context reject.
- Continuous rendering:
  - inline code content отображается monospace/background вместо placeholder;
  - `CodeSpanRun` mapping и локальный edit через ReplaceInlineCode;
  - Enter/newline/empty/invalid edits rejected;
  - code spans внутри link/image/autolink остаются protected, чтобы не менять
    Task 028 contract.
- Tests: rendering, edit, undo/redo, mode switch, backtick collision reject,
  outside bytes, multiline/NUL/invalid, stale token.

**Не включено:**

- Code span creation из toolbar (Code action создаёт fenced block).
- Syntax highlighting.
- Editing code spans inside links/images/autolinks.
- Table-cell code-span structural changes.
- Новые внешние зависимости.

## Ограничения и предположения

- Поддерживаются только source-backed `MD_TEXT_CODE` с одним child в
  `MD_SPAN_CODE`; иначе protected placeholder.
- Backtick delimiters не переписываются; content с collision отвергается AST
  guard.
- Code span content остаётся одной строкой; multi-line inline code protected.
- Code span внутри link/image/autolink остаётся read-only.
- Source bytes вне content range сохраняются.

## Критерии приёмки

1. `` `old` `` отображается как monospace `old`, не placeholder.
2. Редактирование `old` → `new` даёт `` `new` ``; surrounding bytes и
   delimiters сохраняются.
3. Backtick collision, empty/newline/NUL/invalid UTF-8 и unsupported span
   отвергаются без изменения Session.
4. Code span внутри link/image/autolink остаётся protected, link contract не
   меняется.
5. Undo/redo, dirty/token, mode switch и cursor mapping contract сохраняются.
6. Тесты покрывают success/rejection; Windows/Linux CTest и Linux sanitizers
   проходят.

## План проверок

- Engine fixtures: inline edit, backtick collision, outside bytes, CRLF/unicode,
  empty/newline/NUL/invalid, unsupported parent, stale token.
- GUI fixtures: render, edit, undo, mode switch, protected nested link code span.
- Windows UCRT64 Debug: build, CTest, MD4C spike.
- Ubuntu WSL: normal и ASan/UBSan/leaks CTest.
- `git diff --check --ignore-space-at-eol`.
- Native visual QA, clang-format и remote CI отдельно; если недоступны,
  указать явно.

## Зависимости и открытые вопросы

- Зависимости: inline source maps 013, continuous 026, code block 031.
- Открытых блокирующих вопросов нет. Inline code creation/syntax highlighting
  отложены.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.

## Исполнительский промпт

Реализуй только inline code rendering/editing. Добавь `ReplaceInlineCode` и
fixtures, затем `CodeSpanRun`/render path в ContinuousEditor. Сохраняй
backtick delimiters через patch только content range; code spans внутри
link/image/autolink оставь protected. Не добавляй code span creation, syntax
highlighting или новые зависимости. При reject восстанавливай projection.
Проверь Windows/Linux tests и sanitizers; отчёт добавь сюда.

## Итог выполнения

### Реализация

- Добавлен `ReplaceInlineCode`: MD_TEXT_CODE child of MD_SPAN_CODE с exact
  source range, nonempty single-line UTF-8, expected AST update и полный
  `apply` guard; delimiters не переписываются.
- `ContinuousEditor` отображает inline code content monospace/background и
  ведёт `CodeSpanRun`; локальный edit отправляет `ReplaceInlineCode`.
  Enter/newline/empty/NUL/invalid и delimiter collision отвергаются.
- Code spans внутри link/image/autolink остаются protected; Task 028 link
  contract не меняется.
- Engine fixtures покрывают basic/outside/unicode/CRLF, backtick collision,
  empty/newline/NUL/invalid, no-op и stale token. GUI tests покрывают render,
  edit, undo и mode switch; link code-child test остаётся protected.

### Проверки

- Windows UCRT64 Debug: build, **CTest 7/7 passed**; MD4C Qt Test **319
  passed**; GUI smoke **87 passed**.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build with the MD4C spike,
  CTest **7/7 passed**.
- Ubuntu sanitized Debug: ASan/UBSan/leaks with the MD4C spike, CTest **7/7
  passed**.
- `git diff --check --ignore-space-at-eol` прошёл. Native visual QA,
  clang-format, clang-tidy и remote CI не запускались.

### Ограничения

- Code span creation через toolbar не добавлена; Code action создаёт fenced
  block.
- Syntax highlighting, multi-line inline code и code spans внутри link/image/
  autolink остаются вне среза.
- Точный cursor mapping inline code в Markdown не расширялся.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.
- Создан локальный коммит; push не выполнялся.
