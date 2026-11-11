# Task 038 — Создание ссылки из выделенного текста

## Статус и владелец

- Статус: `Done`.
- Владелец: архитектор + C++ разработчик.
- Родитель: [Task 005](task_005.md). Зависимости: 013, 027, 028, 032.

## Контекст

Continuous WYSIWYG умеет редактировать labels существующих inert links, но
Format > Link/toolbar Link остаются disabled. Пользователь не может создать
новую GFM link без Markdown source mode. Link action должен работать только
при выделении внутри одного editable normal text run и сохранять source
preserving contract.

## Цель

Включить Format > Link/toolbar Link в continuous mode: запросить URL,
обернуть выделенный текст в inline link `[label](<url>)`, сохранить все
остальные байты и проверить результат полным AST guard.

## Область работ

**Включено:**

- Engine `WrapInLink { node, textRange, url }`:
  - selection внутри одного source-backed normal text node;
  - nonempty URL/UTF-8, no NUL/newline/CR/angle brackets;
  - selected source label сохраняется, destination в angle brackets;
  - candidate link parse + graft expected AST + общий `apply`;
  - reject link/image nesting, invalid selection, label/punctuation collision.
- DocumentDialogs `chooseLink` (URL prompt, cancellable) и TestDialogs override.
- Continuous UI:
  - `linkCreationEnabled()` и `insertLink(url)`;
  - Link action enabled в continuous mode только при валидном selection;
  - cancel dialog не меняет Session.
- Format menu/toolbar Link action; Image остаётся disabled.
- Engine/GUI tests для selection, URL escaping, cancel, undo/redo, mode switch,
  invalid selection/URL, stale token.

**Не включено:**

- Image creation.
- Link title prompt, reference-style link creation, collapsed/shortcut links.
- Link editing addresses/titles и navigation.
- Markdown-mode creation, multi-run selection, cross-block selection.
- Новые внешние зависимости.

## Ограничения и предположения

- Label — исходный selected source text внутри одного normal run; formatting
  внутри selection допустимо только если selection не пересекает runs.
- Destination всегда записывается как `<url>`; URL с `>`/`<`/пробелами/
  newline/NUL отвергается.
- Link создаётся inline; definitions не добавляются.
- Link/image ancestor reject, чтобы не создавать nested/ambiguous links.
- При cancel/ошибке source, dirty/token и selection не меняются.

## Критерии приёмки

1. Format > Link/toolbar Link enabled только в continuous mode при непустом
   selection внутри одного normal run.
2. `alpha` → `[alpha](<https://example.test>)` через UI; остальные байты
   сохраняются.
3. URL с parentheses/обычным текстом корректен; URL с `<>`, пробелами,
   newline, NUL или invalid UTF-8 отвергается.
4. Link внутри existing link/image, multi-run/cross-block selection и
   empty selection rejected; actions disabled или explicit error.
5. Cancel URL dialog не создаёт history/dirty изменения.
6. Undo/redo, mode switch, token и source preservation сохраняются.
7. Windows/Linux CTest и Linux sanitizers проходят.

## План проверок

- Engine fixtures: full/partial run, URL parentheses, outside bytes, nested
  link/image rejection, invalid URL/range, stale token.
- GUI fixtures: action enabled state, mocked URL, cancel, undo, mode switch.
- Windows UCRT64 Debug: build, CTest, MD4C spike.
- Ubuntu WSL: normal и ASan/UBSan/leaks CTest.
- `git diff --check --ignore-space-at-eol`.
- Native visual QA, clang-format и remote CI отдельно; если недоступны,
  указать явно.

## Зависимости и открытые вопросы

- Зависимости: inline source maps 013, inline formatting 027, link labels 028.
- Открытых блокирующих вопросов нет. Image creation и reference links
  отложены.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.

## Исполнительский промпт

Реализуй только inline link creation из одного normal text selection. Добавь
`WrapInLink` и fixtures, `DocumentDialogs::chooseLink`, затем UI action и
tests. Используй source-preserving parse/graft/apply; не добавляй image
creation, title/reference link dialogs, navigation или Markdown-mode
insertion. При cancel/reject не меняй Session. Проверь Windows/Linux tests и
sanitizers; отчёт добавь сюда.

## Итог выполнения

### Реализация

- Добавлен `WrapInLink`: mapped normal text node и UTF-8 range, URL в angle
  brackets, label source сохраняется; candidate link parse, graft expected AST
  и полный `apply` guard. Nested links/images, invalid URL/range и пустой
  selection отвергаются.
- `DocumentDialogs::chooseLink` добавлен (QInputDialog URL, cancel-safe);
  TestDialogs override возвращает заданный URL.
- `ContinuousEditor::linkSelection`/`linkCreationEnabled`/`insertLink`:
  selection только внутри одного normal run, surrogate и cross-run guards;
  Link action enabled только при валидном selection.
- Format menu/toolbar Link подключены; Image остаётся disabled. Cancel URL
  dialog не меняет Session; undo/redo/mode switch сохраняются.
- Engine fixtures покрывают full/partial/formatted label, parentheses URL,
  nested link/image reject, invalid URL/range и stale token. GUI tests
  покрывают action enabled state, mocked URL, cancel, undo и mode switch.

### Проверки

- Windows UCRT64 Debug: build, **CTest 7/7 passed**; MD4C Qt Test **307
  passed**; GUI smoke **85 passed**.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build with the MD4C spike,
  CTest **7/7 passed**.
- Ubuntu sanitized Debug: ASan/UBSan/leaks with the MD4C spike, CTest **7/7
  passed**.
- `git diff --check --ignore-space-at-eol` прошёл. Native visual QA,
  clang-format, clang-tidy и remote CI не запускались.

### Ограничения

- Создаются только inline links; titles, reference/collapsed/shortcut links и
  definitions не поддерживаются.
- Label ограничен одним normal text run; formatting внутри selection
  допускается только если selection не пересекает runs.
- URL с `<`, `>`, пробелами, newline, NUL или invalid UTF-8 отвергается.
- Image creation, link address/title editing и navigation не добавлены.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.
- Создан локальный коммит; push не выполнялся.
