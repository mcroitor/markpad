# Task 006 — Постраничный WYSIWYG и настройки страницы

## Статус и владелец

- Статус: `Done` (первый paged-срез: layout engine + editable paged mode)
- Владелец: архитектор + C++ разработчик
- Приоритет: P1

## Контекст

Третий режим — редактируемый WYSIWYG, который показывает те же данные в виде страниц с автоматической раскладкой. Sketch предусматривает Page Size, Orientation и Margins. Этот режим и PDF должны использовать одни правила layout.

## Цель

Добавить постраничный режим редактирования и тестируемый layout engine.

## Область работ

**Включено:** PageSetup value object, автоматическая раскладка по страницам, видимые листы/поля/границы, редактирование в paginated workspace, навигация и номер страниц в статусной строке, layout tests.

**Не включено:** сохранение page setup в Markdown, печать/PDF, ручные page breaks, если не будут утверждены отдельно.

## Ограничения и предположения

- Использовать общую GFM document model и редактор из Task 005.
- Layout учитывает размеры страниц и поля; текст остаётся редактируемым.
- Не добавлять специальные синтаксические разрывы страниц, отсутствующие в GFM, без отдельного требования.

## Критерии приёмки

1. Paginated mode позволяет редактировать документ, отображает страницы и согласованно перестраивает layout при изменении текста/шрифта/настроек.
2. Page Size, Orientation, Margins применяются корректно и валидируются.
3. Проверены заголовки, абзацы, списки, длинные блоки кода, таблицы и изображения у границ страниц.
4. Layout доступен как независимый от окон компонент и имеет детерминированные тесты.

## План проверок

- Layout unit tests для page geometry, margins, boundary cases и content blocks.
- GUI tests на редактирование/перерасчёт/счётчик страниц.
- Ручное сравнение Windows/Linux; допустимая разница метрик шрифтов документируется.

## Зависимости и открытые вопросы

- Зависимости: Task 005.
- Решения Task 010: Page Setup — глобальные defaults в Settings Service (Task
  009), A4 portrait/20 мм по умолчанию, значения конфигурируемы; блок выше
  страницы переносится на новую и может overflow с видимым предупреждением, без
  внутреннего split. Auto Save отложен и не блокирует этот режим.

## Исполнительский промпт

Реализуй Task 006 только после перехода в `Ready` и утверждения Task 010 решений о хранении и дефолтах. Используй одну PageLayout для paginated editing и будущего PDF. Сохраняй редактируемость текста; не реализуй только визуальную печать. Не вводи page-break markup в GFM. Добавь unit tests геометрии/переносов и интеграционные tests режима. Особо проверь большие таблицы/кодовые блоки и блоки выше высоты страницы; не скрывай неподдерживаемое поведение. Проверь обе платформы, если доступны, иначе укажи ограничения.

## Итог выполнения

### Реализация

- `src/page_layout.{h,cc}`: независимый `PageLayoutEngine` с `PageGeometry`,
  `BlockPlacement` и `PageLayoutResult`. Поддержаны A4/Letter/Legal,
  portrait/landscape, четыре margins, перевод mm→points и deterministic
  block placement. PageSize/margins валидируются; блок выше content area
  переносится на новую страницу и помечается overflow с warning.
- `src/page_text_edit.{h,cc}`: editable `QTextEdit`-based paged surface:
  ширина текста следует page content width, page guides/separators и page labels
  рисуются по block placements, page count/current page доступны.
- `ContinuousEditor` получил `setPaged`/`setPageSetup`/`pageCount`/`currentPage`/
  `layoutWarning`; rich editing, selection, undo/history и continuous mode
  используют одну session/model.
- `MainWindow`: View → WYSIWYG (Page layout), status bar `Page N of M`,
  overflow indicator, page setup из global Settings (A4 portrait/20 мм по
  умолчанию), live перерасчёт при вводе/смене настроек.
- Page setup хранится в Settings Service (Task 009), не в Markdown. Page size,
  orientation и margins меняются через Edit → Settings; per-document metadata и
  Auto Save не вводятся.
- Добавлены layout tests: geometry, A4/landscape/margins, paragraph flow,
  headings/lists/code/tables/images, block boundaries, oversized block,
  deterministic placements и invalid margins.

### Проверки

- Windows UCRT64 Debug: build, **CTest 6/6 passed**; `markpad_layout`
  **9 passed**; GUI smoke **74 passed**; MD4C suite **232 passed**.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build, CTest **6/6 passed**.
- Ubuntu sanitized Debug: ASan/UBSan/leaks, CTest **6/6 passed**.
- `git diff --check --ignore-space-at-eol` прошёл; сборка без warnings.
  Google clang-format в среде отсутствует; dry-run не выполнялся.
- Native visual QA и remote CI не запускались.

### Ограничения

- Это paged editing surface с page guides/page count, а не отдельные
  page-widgets/листы с полным фоном и типографскими полями; текст остаётся
  единым editable rich-text документом. Page layout metadata и границы страниц
  определяются независимым engine.
- Блоки не split-ятся внутри; block выше content area занимает новую страницу и
  overflow-ит с предупреждением. Manual page breaks не добавлялись.
- Page metrics зависят от шрифта/DPI платформы; различия Windows/Linux
  документируются как допустимые. PDF/print не реализованы.
- Line-number gutter и Auto Save остаются вне среза. Коммит/push не создавались.
