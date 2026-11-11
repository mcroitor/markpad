# Task 030 — Blockquote и списки в continuous WYSIWYG

## Статус и владелец

- Статус: `Done`.
- Владелец: архитектор + C++ разработчик.
- Родитель: [Task 005](task_005.md). Зависимости: 014–020, 026–029.

## Контекст

Continuous WYSIWYG уже умеет текстовые правки, заголовки, task-state,
inline-форматирование, inert links и alt-подписи. Engine имеет проверенные
операции цитат и списков (`WrapInQuote`, `UnwrapQuote`, `SetListKind`,
`InsertListItem`, `RemoveListItem`, `SetItemTask`, `IndentListItem`,
`OutdentListItem`), но Format-меню по-прежнему показывает Blockquote и
Ordered/Unordered выключенными. Пользователь не может создать цитату или
список без перехода в Markdown.

## Цель

Разрешить безопасное структурное редактирование текущего блока в continuous
WYSIWYG: включить/выключить blockquote и преобразовать top-level paragraph в
ordered/unordered list либо сменить kind существующего списка, сохраняя
исходные байты вне изменяемого диапазона и общий Session/history contract.

## Область работ

**Включено:**

- Новый source-preserving `WrapInList` engine command для одного top-level
  paragraph:
  - paragraph → UL (`- `) или OL (`1. `);
  - поддержка soft line breaks простого paragraph через continuation indent;
  - сохранение EOL (`LF`/`CRLF`/`CR`), reference links/definitions, inline
    spans и всех байтов вне paragraph range;
  - полный AST guard через повторный parse; при неоднозначности — reject.
- UI-интеграция continuous:
  - Blockquote: `WrapInQuote` для текущего блока и `UnwrapQuote` для
    ближайшей enclosing quote;
  - Ordered/Unordered: `SetListKind` для существующего списка и `WrapInList`
    для top-level paragraph;
  - контекстный enabled/tooltip state; multi-block selection и
    неподдержанные блоки отклоняются явно.
- MainWindow Format menu/toolbar: Blockquote и Ordered/Unordered остаются
  видимыми, но теперь включаются только при доступной операции.
- Engine и GUI Qt Test для success/rejection, undo/redo, dirty/token и
  mode-switch сценариев.

**Не включено:**

- Multi-block selection и создание списка из нескольких абзацев.
- Удаление списка (list → paragraphs), list item insert/remove/indent/outdent
  UI и вложенные списки сверх уже поддержанного engine mapping.
- Создание списка внутри blockquote, list item или другого контейнера.
- Fenced code, table/HTML/HR rendering и Format actions Code/Link/Image/Table.
- Новые dialogs, URI policies, external dependencies или глобальная
  Markdown-пересериализация.

## Ограничения и предположения

- `WrapInList` работает только для paragraph с parent `MD_BLOCK_DOC` и без
  ведущих пробелов/табов; остальные случаи явно отвергаются.
- Смена kind существующего списка использует существующий `SetListKind`; для
  ordered списка сохраняется текущий start.
- Blockquote для tight list item (LI без paragraph) оборачивает содержащий
  список; blockquote для table/HTML/HR остаётся disabled, потому что engine не
  даёт для них source-preserving quote range.
- Cursor/selection mapping остаётся прежним: structural actions применяются
  только когда selection находится внутри одного rendered context.
- Source bytes вне target range не меняются; глобальная нормализация и
  fallback на прямую замену source запрещены.
- Неизвестные/защищённые GFM nodes не должны теряться при изменении списка
  или цитаты.

## Критерии приёмки

1. `alpha\n` → `- alpha\n` и `1. alpha\n` через `WrapInList`; AST содержит
   ровно один tight LI, а source вне range не изменён.
2. Простой multi-line paragraph с soft break преобразуется в один LI с
   корректным continuation indent; mixed EOL сохраняется.
3. `SetListKind` переключает top-level и уже поддержанный nested list между
   ordered/unordered без потери task-state, items и definitions.
4. Blockquote action оборачивает текущий paragraph/heading/list/code в quote и
   снимает ближайшую quote; source вне блока не меняется.
5. В continuous UI действия Blockquote/Ordered/Unordered enabled только для
   допустимого контекста; multi-block, неподдержанные контейнеры и
   list/table/HR-сценарии без source map дают disabled/tooltip или явную
   ошибку. Blockquote для code block доступен; List для code block — нет.
6. Undo/redo, dirty checkpoint, token, mode switch и повторный parse сохраняют
   общий Session contract; stale token и no-op не создают лишнюю history.
7. Engine и GUI tests покрывают success/rejection; Windows/Linux CTest и Linux
   sanitizers проходят.

## План проверок

- Engine fixtures: UL/OL creation, start, multi-line, CRLF, definitions,
  leading indentation rejection, non-paragraph rejection, no-op/stale token.
- GUI fixtures: menu enablement, blockquote wrap/unwrap, list creation/kind
  switch, undo/redo, dirty, mode switch, multi-block rejection.
- Windows UCRT64 Debug: build, CTest, MD4C spike.
- Ubuntu WSL: normal и ASan/UBSan/leaks CTest.
- `git diff --check --ignore-space-at-eol`.
- Native visual QA, clang-format и remote CI отдельно; если недоступны,
  указать явно.

## Зависимости и открытые вопросы

- Зависимости: engine Tasks 014–020, continuous Tasks 026–029.
- Открытых блокирующих вопросов нет. Создание списка из multi-block selection,
  удаление списка и вложенные контейнеры осознанно отложены.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.

## Исполнительский промпт

Реализуй только описанный срез. Сначала добавь `WrapInList` в
`markpad::markdown` и его fixtures, затем контекстные state/methods в
`ContinuousEditor`, затем подключи Format actions в `MainWindow`. Используй
существующие quote/list source maps и `apply` AST guard; не добавляй обходной
путь через прямую замену source или глобальную сериализацию. Не расширяй scope
на code/table/HTML/HR, list removal, multi-block selection или новые dialogs.
Каждый reject должен возвращать понятную причину и не менять Session. Tests —
Qt Test; проверь Windows/Linux и sanitizers. Отчёт о выполнении добавь сюда.

## Итог выполнения

### Реализация

- В `markpad::markdown` добавлена source-preserving команда `WrapInList` для
  одного top-level paragraph. Она принимает parent root, позицию paragraph,
  ordered flag и start; генерирует `- ` или `<start>. ` marker, добавляет
  continuation indent для soft line breaks и сохраняет EOL/все байты вне
  диапазона paragraph.
- Новый command использует существующий `parseListContext` с definition
  context, поэтому reference links и definitions не теряют binding.
  Structural guard проверяет ровно один tight LI и ordered start; при
  неоднозначности команда отвергается.
- `ContinuousEditor` теперь хранит в context текущий block, ближайшие list и
  quote. Добавлены `blockquoteState`, `listState`, `toggleBlockquote` и
  `changeListKind` с enabled/tooltip state и явными reject reasons.
- Blockquote action использует существующие `WrapInQuote`/`UnwrapQuote`:
  оборачивает paragraph/heading/code, а для tight list item — содержащий его
  list; повторное нажатие снимает ближайшую quote.
- Ordered/Unordered actions вызывают `SetListKind` для существующего списка и
  `WrapInList` для top-level paragraph. Code/table/HR placeholders получают
  context только там, где quote engine реально поддерживает операцию.
- Format menu/toolbar actions Blockquote, Ordered и Unordered больше не
  выключены постоянно; `refreshState` включает их только в continuous mode и
  при доступном контексте. Code/Link/Image/Table остаются disabled.
- Добавлены engine fixtures для UL/OL, start, multi-line, CRLF, EOF,
  reference definitions, indentation и rejection. GUI tests покрывают
  blockquote wrap/unwrap, list creation/kind switch, list quote, code quote,
  multi-block disabled, undo/redo и dirty state.

### Проверки

- Windows UCRT64 Debug: build, **CTest 7/7 passed**; MD4C Qt Test **249
  passed**; GUI smoke **76 passed**.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build with the MD4C spike,
  CTest **7/7 passed**.
- Ubuntu sanitized Debug: ASan/UBSan/leaks with the MD4C spike, CTest **7/7
  passed**.
- `git diff --check --ignore-space-at-eol` прошёл. Native visual QA,
  clang-format, clang-tidy и remote CI не запускались.

### Ограничения

- `WrapInList` поддерживает только один top-level paragraph без ведущих
  пробелов/табов; multi-block, nested paragraph→list и list→paragraph
  остаются вне среза.
- Смена kind существующего списка использует `SetListKind`; list item
  insert/remove/indent/outdent UI не добавлен.
- Blockquote для tight list item трактуется как оборачивание содержащего
  списка; для loose item оборачивается paragraph. Table/HTML/HR blockquote
  остаются disabled.
- Code/table/HR rendering и Code/Link/Image/Table creation не реализованы.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.
- Создан локальный коммит; push не выполнялся.
