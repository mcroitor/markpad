# Task 037 — Code block и Horizontal Rule insertion/rendering

## Статус и владелец

- Статус: `Done`.
- Владелец: архитектор + C++ разработчик.
- Родитель: [Task 005](task_005.md). Зависимости: 031, 036.

## Контекст

Format menu содержит Code и Horizontal Line, но обе actions остаются disabled.
Fenced code body уже редактируется (Task 031), а top-level block insertion
контракт реализован для таблиц (Task 036). Horizontal rule всё ещё показан как
placeholder. Пользователь не может вставить code block или horizontal rule без
Markdown mode.

## Цель

Включить Code и Horizontal Line в continuous WYSIWYG: вставлять пустой fenced
code block и horizontal rule после текущего top-level block/table или в конец
документа, а HR отображать как визуальную линию. Source должен сохраняться вне
insertion range и проходить общий Session/history contract.

## Область работ

**Включено:**

- Engine `InsertHorizontalRule` и `InsertFencedCode`:
  - top-level insertion после текущего блока/таблицы или в конец документа;
  - blank-line separators и EOL документа;
  - `---` для HR и пустой fenced code block (```…```);
  - parse/graft/apply guard, bytes вне insertion range неизменны.
- Continuous UI:
  - `insertHorizontalRule()` и `insertFencedCode()`;
  - Code/Horizontal Line actions активны только в continuous mode;
  - пустой code body использует существующий editable CodeRun;
  - HR рендерится как центрированная protected line, а не placeholder.
- Tests: empty/after/before/middle/CRLF insertion, action enabled state,
  rendering, undo/mode switch, invalid position/stale token.

**Не включено:**

- Code language/info string picker.
- Code block creation в Markdown mode.
- Link/Image creation.
- Rich code syntax highlighting, HR styling/themes, nested code/HR inside
  list/quote.
- Новые внешние зависимости.

## Ограничения и предположения

- Default code block: пустой fenced block без info string.
- Default HR: `---`.
- Insertion point — после текущего top-level block/table или в конец документа;
  используется тот же insertion contract, что у Task 036.
- HR — protected visual line; его source редактируется в Markdown, WYSIWYG
  structural commands для HR не добавляются.
- Code actions доступны только в continuous mode.
- Reject не меняет Session.

## Критерии приёмки

1. Format > Code/toolbar Code активен в continuous mode и disabled в Markdown.
2. Вставка code block создаёт пустой fenced block после текущего блока/в конец;
   source вне insertion range сохраняется.
3. Format > Horizontal Line/toolbar Horizontal Line вставляет `---` с
   правильными blank-line separators/EOL и рендерит видимую линию.
4. Пустой code body можно заполнить через existing code editing path.
5. CRLF документ получает CRLF code/HR.
6. Undo/redo, dirty/token/mode switch сохраняются; invalid position/stale token
   не меняют source.
7. Windows/Linux CTest и Linux sanitizers проходят.

## План проверок

- Engine fixtures: code/HR empty/after/before/middle/CRLF, invalid position,
  stale token.
- GUI fixtures: action enabled state, insertion, HR line presence, undo/mode
  switch.
- Windows UCRT64 Debug: build, CTest, MD4C spike.
- Ubuntu WSL: normal и ASan/UBSan/leaks CTest.
- `git diff --check --ignore-space-at-eol`.
- Native visual QA, clang-format и remote CI отдельно; если недоступны,
  указать явно.

## Зависимости и открытые вопросы

- Зависимости: code rendering/editing 031, block insertion 036.
- Открытых блокирующих вопросов нет.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.

## Исполнительский промпт

Реализуй только Code/Horizontal Line insertion и HR rendering. Используй общий
top-level insertion contract Task 036, не добавляй size/info dialogs, Markdown
mode insertion, link/image creation или syntax highlighting. Code block должен
быть пустым fenced block; HR — `---` и protected visual line. Все изменения
через Session/history и apply guard. Добавь engine/GUI tests и отчёт здесь.

## Итог выполнения

### Реализация

- Общий top-level insertion helper вынесен из Task 036 и используется
  `InsertTable`, `InsertHorizontalRule` и `InsertFencedCode`: boundary после
  текущего блока/таблицы или EOF, blank-line separators, EOL документа,
  parse/graft/apply guard.
- `InsertHorizontalRule` вставляет `---`; `InsertFencedCode` вставляет пустой
  fenced block без info string. Source bytes вне insertion range сохраняются.
- Continuous UI: Code и Horizontal Line actions активны только в continuous
  mode; пустой code body использует существующий CodeRun editing, HR
  отображается центрированной protected линией вместо placeholder.
- `mapFencedCodeRange` теперь допускает пустой closed fenced block без
  source-backed text; добавлен `createdEmptyCodeSourceMap` fixture.
- Engine fixtures покрывают empty/after/before/CRLF HR/code, invalid position и
  stale token. GUI tests покрывают enabled state, insertion, empty code body
  typing, HR rendering, undo и mode switch.

### Проверки

- Windows UCRT64 Debug: build, **CTest 7/7 passed**; MD4C Qt Test **301
  passed**; GUI smoke **84 passed**.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build with the MD4C spike,
  CTest **7/7 passed**.
- Ubuntu sanitized Debug: ASan/UBSan/leaks with the MD4C spike, CTest **7/7
  passed**.
- `git diff --check --ignore-space-at-eol` прошёл. Native visual QA,
  clang-format, clang-tidy и remote CI не запускались.

### Ограничения

- Code block создаётся пустым и без info string; language picker не добавлен.
- HR — protected visual line; WYSIWYG-редактирование source HR не добавлено.
- Code/HR creation доступны только в continuous mode.
- Nested/list/quote variants, syntax highlighting и HR theming не включены.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.
- Создан локальный коммит; push не выполнялся.
