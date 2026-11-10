# Task 039 — Создание изображения из выделенного alt-текста

## Статус и владелец

- Статус: `Done`.
- Владелец: архитектор + C++ разработчик.
- Родитель: [Task 005](task_005.md). Зависимости: 013, 028, 029, 038.

## Контекст

Continuous WYSIWYG редактирует alt-подписи существующих images и создаёт
inline links (Task 038), но Format > Image/toolbar Image остаются disabled.
Пользователь не может добавить GFM image без Markdown source mode.
Image creation должен использовать тот же selection/source-preserving contract,
не загружая внешние ресурсы.

## Цель

Включить Format > Image/toolbar Image в continuous mode: запросить source URL,
использовать выделенный текст как alt и заменить selection на инертный GFM
image `![alt](<src>)`, сохранив все окружающие байты и пройдя полный AST guard.

## Область работ

**Включено:**

- Engine `InsertImage { node, textRange, src }`:
  - selection внутри одного mapped normal text run;
  - nonempty src/UTF-8, no NUL/newline/CR/angle brackets/whitespace;
  - selected source label становится alt;
  - candidate image parse + graft expected AST + общий `apply`;
  - reject nested link/image, invalid range/src и structural mismatch.
- DocumentDialogs `chooseImage` (source URL prompt, cancellable) и TestDialogs
  override.
- Continuous UI:
  - `imageCreationEnabled()` и `insertImage(src)`;
  - Image action enabled в continuous mode только при валидном selection;
  - cancel dialog не меняет Session.
- Format menu/toolbar Image action. Link action остаётся.
- Engine/GUI tests для selection, source escaping, cancel, undo/redo, mode
  switch, invalid selection/src, nested link/image, stale token.

**Не включено:**

- Image loading/preview/resource access.
- Image title prompt, reference-style images, alt editing через новый command.
- Markdown-mode creation, multi-run/cross-block selection.
- Image size/alignment/themes.
- Новые внешние зависимости.

## Ограничения и предположения

- Alt — исходный selected source text внутри одного normal run; formatting
  selection внутри одного run допустимо, multi-run нет.
- Source всегда записывается в angle brackets `<src>`; URL с `<>`, пробелами,
  newline, NUL или invalid UTF-8 отвергается.
- Image создаётся inline; definitions/titles не добавляются.
- Nested link/image ancestor reject, чтобы не создавать неоднозначную
  структуру.
- Cancel/ошибка не меняют source, dirty/token и selection.
- Ресурсы не загружаются; image остаётся inert alt-label.

## Критерии приёмки

1. Format > Image/toolbar Image enabled только в continuous mode при непустом
   selection внутри одного normal run.
2. `alpha` → `![alpha](<https://example.test/image.png>)` через UI; остальные
   байты сохраняются.
3. Source с parentheses допустим; source с `<>`, пробелами, newline, NUL или
   invalid UTF-8 отвергается.
4. Selection across runs/blocks, empty selection и selection внутри existing
   link/image отвергаются; actions disabled или explicit error.
5. Cancel source dialog не создаёт history/dirty изменения.
6. Undo/redo, mode switch, token, source preservation и inert rendering
   сохраняются.
7. Windows/Linux CTest и Linux sanitizers проходят.

## План проверок

- Engine fixtures: full/partial label, parentheses source, outside bytes,
  nested link/image reject, invalid src/range, stale token.
- GUI fixtures: action enabled state, mocked source, cancel, undo, mode switch.
- Windows UCRT64 Debug: build, CTest, MD4C spike.
- Ubuntu WSL: normal и ASan/UBSan/leaks CTest.
- `git diff --check --ignore-space-at-eol`.
- Native visual QA, clang-format и remote CI отдельно; если недоступны,
  указать явно.

## Зависимости и открытые вопросы

- Зависимости: inline source maps 013, image alt editing 028/029, link creation
  038.
- Открытых блокирующих вопросов нет. Image titles/reference/style отложены.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.

## Исполнительский промпт

Реализуй только inline image creation из одного normal text selection. Добавь
`InsertImage` и fixtures, `DocumentDialogs::chooseImage`, затем UI action и
tests. Используй source-preserving parse/graft/apply; не добавляй image
loading, title/reference dialogs, image styling или Markdown-mode insertion.
При cancel/reject не меняй Session. Проверь Windows/Linux tests и sanitizers;
отчёт добавь сюда.

## Итог выполнения

### Реализация

- Добавлен `InsertImage`: selected normal text становится alt, src пишется в
  angle brackets, candidate image parse, graft expected AST и полный `apply`
  guard. Nested link/image, invalid src/range и пустой selection отвергаются.
- `DocumentDialogs::chooseImage` добавлен (QInputDialog image URL, cancel-safe);
  TestDialogs override возвращает заданный URL.
- `ContinuousEditor` использует общий selection path для link/image:
  `imageCreationEnabled`/`insertImage`, guards для одного normal run и
  surrogate/cross-run selection.
- Format menu/toolbar Image подключены; все Format actions теперь имеют
  implementation. Cancel source dialog не меняет Session; undo/redo/mode
  switch сохраняются.
- Engine fixtures покрывают full/partial/formatted alt, parentheses src,
  nested link/image reject, invalid src/range и stale token. GUI tests
  покрывают action enabled state, mocked source, cancel, undo и mode switch.

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

- Создаются только inline images; titles, reference-style images и image
  sizing/alignment не поддерживаются.
- Alt ограничен одним normal text run; selection внутри existing link/image
  отвергается.
- Image source с `<>`, пробелами, newline, NUL или invalid UTF-8 отвергается;
  ресурсы не загружаются.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.
- Создан локальный коммит; push не выполнялся.
