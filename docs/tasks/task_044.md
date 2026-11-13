# Task 044 — Настоящая редактируемая постраничная поверхность

## Статус и владелец

- Статус: `Done`.
- Владелец: архитектор/координатор; `cpp-developer`, `test-writer`, при необходимости
  `frontend-designer`.
- Приоритет: P1 (release blocker после QA).
- Решение владельца (2026-11-12): вариант A, incremental page sheets.

## Контекст

Текущий paged WYSIWYG — один `QTextEdit` с уменьшенной шириной текста и
пунктирными page-guide линиями. Это не выглядит как реальные листы бумаги:
нет page sheets, полей страницы, промежутков между страницами и корректного
разрыва содержимого на границе листа. Пользователь требует block-based
page surface с расчётом позиционирования блоков.

## Цель

Показать документ как набор реальных страниц с полями, разрывами и номерами,
сохранив редактирование в paged WYSIWYG и source-preserving контракт Markdown.

## Область работ

**Включено:**

- Модель разбиения документа на страницы на основе AST/блочного layout.
- Page sheets с размерами A4/Letter/Legal, orientation и margins.
- Разрывы только между блоками; oversized block переносится на отдельную
  страницу и даёт overflow warning в соответствии с Task 006.
- Таблица и code block рассматриваются как цельные block units и не разрезаются
  между page sheets; row-level split, если понадобится, должен быть отдельным
  явно принятым контрактом.
- Редактирование текста, заголовков, списков, code, links/images, tables и HR
  в постраничном режиме.
- Синхронизация source ranges, token, undo/redo, cursor/navigation и dirty state.
- Согласование page count с PDF-export там, где это допускает Task 008; различие
  Qt-layout/PDF документируется; PDF parity — Task 047.
- Учесть исправления Task 045 (dark theme) и Task 046 (backgrounds/code/table fit)
  в page sheets.
- Тесты разрывов, больших блоков, навигации и round-trip.

**Не включено:**

- Изменение GFM-модели или добавление page-break markup.
- WYSIWYG typography settings (Task 043).
- Печать на физическом принтере и CI-инфраструктура.

## Выбранная архитектура

**A. Один editable document + page-break spacer blocks (инкрементальный).**
Реализуется как первый release milestone; per-page canvases (B) остаются
возможным последующим развитием, если визуальная модель A окажется недостаточной.

### A. Один editable document + page-break spacer blocks (инкрементальный)

`PageLayoutEngine` определяет границы страниц; в rich-text projection вставляются
нередактируемые spacer blocks, а `paintEvent` рисует page sheets, поля и номера.
Текст остаётся в одном `QTextEdit`.

Плюсы: сохраняет текущий editing/source-mapping, наименьший риск и объём.
Минусы: страницы не являются независимыми editors; нужна аккуратная обработка
spacer blocks в mapping/selection; разрывы только между блоками.

### B. Per-page editable canvases (полноценные страницы)

AST-блоки разбиваются на страницы; каждая страница получает собственный
editable rich-text canvas; scroll area показывает листы с промежутками.

Плюсы: настоящие листы, независимое позиционирование, проще визуально.
Минусы: крупный рефакторинг mapping, истории, таблиц, навигации между страницами;
несколько этапов реализации.

### C. Read-only page preview + continuous editor

Отдельный preview настоящих страниц, редактирование остаётся в continuous mode.

Плюсы: быстрый визуальный результат.
Минусы: нарушает требование трёх редактируемых режимов; не рекомендуется.

### D. QGraphicsScene с блоками как items

Каждый блок — отдельный item/editor на page scene.

Плюсы: максимальная гибкость в стиле Word.
Минусы: самый большой объём и риск; не рекомендуется для текущего релиза.

## Ограничения и предположения

- Страницы и поля должны совпадать с `PageLayoutEngine`/PDF geometry в рамках
  принятого Task 006 контракта; pixel-perfect Qt/PDF не обещается.
- Один блок не разрывается внутри; block выше страницы overflow-ит.
- Нельзя потерять Markdown source bytes, undo/redo и mode switching.
- Нужно сохранить работу с таблицами и inline-командами.

## Критерии приёмки

1. Paged mode показывает отдельные page sheets с полями, промежутками и номерами,
   а не пунктирные guides поверх непрерывного текста.
2. Блок не пересекает нижнюю границу страницы; oversized block даёт overflow.
3. Редактирование текста/заголовков/списков/code/links/images/tables/HR в paged
   mode сохраняет source, token и историю так же, как continuous mode.
4. Cursor/navigation/page count корректны после правок и смены page setup.
5. Тесты покрывают многостраничный fixture, большие блоки, смену margins/orientation
   и round-trip source.
6. Широкая таблица не разрезается между страницами и остаётся полностью видимой
   в пределах page content area (или row-level split отдельно принят owner).
7. README/ARCHITECTURE и screenshots обновлены; PDF/paged divergence документирован.

## План проверок

- Unit tests для page partition и block placement.
- GUI integration tests для правок и навигации на нескольких страницах.
- Windows/Linux CTest + sanitizers.
- Визуальные screenshots на фиксированном fixture; сравнение page count с PDF.

## Зависимости и открытые вопросы

- Зависимости: Tasks 005, 006, 043; Tasks 045/046 для итоговой визуальной
  согласованности; Task 047 зависит от этой задачи.
- Выбрано: A как release milestone; B (per-page canvases) — возможное later
  развитие, если A не удовлетворит acceptance.

## Исполнительский промпт

Реализуй выбранный вариант A в `PageTextEdit`/`ContinuousEditor`: построй
page sheets с полями, промежутками и номерами из `PageLayoutEngine`, добавь
блоковые page-break spacing на границах страниц, не меняя GFM source bytes,
token, undo/redo и mapping. Не добавляй неразрывный split блока; oversized
block должен по-прежнему давать overflow warning. Покрой тестами многостраничный
fixture, навигацию, page setup и round-trip; обнови README/ARCHITECTURE и
screenshots. Если mapping или editing ломается, остановись и верни отчёт с
конкретным blocker вместо частичной имитации страниц.

## Итог выполнения

Ожидает реализации. Код не изменялся.

## Implementation result (2026-11-12)

Option A was implemented without introducing a second editor:

- Paged mode now draws real centered page sheets, gray desk gaps, page borders
  and page numbers. The document margin and viewport margins follow the
  configured page size/orientation/margins.
- Page breaks are represented by block/table top margins, so no spacer text is
  inserted into the Markdown document. Switching back to continuous mode and
  re-rendering resets the page spacing.
- `PageLayoutEngine` now includes block bottom margins in page placement and
  treats all blocks of a table and all blocks carrying the same page-unit
  property as one placement. Fenced code blocks are marked by `ContinuousEditor`
  with that property, so tables/code blocks stay whole on a page.
- Editing, selection, source ranges, token revision, custom undo/redo and dirty
  state remain on the single editable document. Paged-mode source/token
  preservation and an edit+undo round-trip are covered by the smoke test.
- Automated checks: `markpad_layout_tests` covers page partition, table/code
  atomicity, overflow and page-for-position; `markpad_smoke` covers page-sheet
  creation, page-2 spacing, source/token preservation and paged editing.
- Evidence: `docs/qa/task_044/paged.png`,
  `docs/qa/task_044/paged-page-gap.png`.
- Remaining: PDF parity is still [Task 047](task_047.md); owner visual sign-off
  is still required.

### Follow-up: A4 scaling, margins and justification (2026-11-12)

- The first page-sheet pass interpreted A4 point dimensions as screen pixels,
  which made the sheet look like A5/A6. `PageTextEdit` now scales page size and
  margins by `logicalDotsPerInch / 72`; at 96 DPI an A4 page is about
  794 x 1123 px, so a 12 pt single-spaced page holds the expected 400-500 words.
- `QTextDocument::textWidth` is the total document width, not the content width.
  The page now sets text width to the full page width and lets the document
  margin produce the content area, so left and right margins are equal
  (verified by a regression assertion).
- Body paragraphs and list text use `Qt::AlignJustify`; headings, code and table
  cells retain their own alignment.
- QA evidence was regenerated in light and dark themes:
  `docs/qa/task_044/paged.png`, `paged-page-gap.png` and `dark/*.png`.