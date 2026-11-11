# Task 033 — Первый срез визуальных таблиц в continuous WYSIWYG

## Статус и владелец

- Статус: `Done`.
- Владелец: архитектор + C++ разработчик.
- Родитель: [Task 005](task_005.md). Зависимости: 015, 021–024, 026–032.

## Контекст

GFM tables имеют проверенный source-map и engine commands Tasks 021–024, но
continuous WYSIWYG показывает таблицу как `[Read-only: table - edit in
Markdown]`. Пользователь не видит структуру таблицы и не может править даже
простые cell text без перехода в Markdown.

## Цель

Показать top-level GFM table как настоящую таблицу QTextTable с header/body и
alignment, разрешить локальное редактирование normal text runs внутри ячеек
через существующий source-preserving ReplaceText, а неподдержанные таблицы
оставить защищёнными placeholder без потери source.

## Область работ

**Включено:**

- Top-level `MD_BLOCK_TABLE` rendering через QTextTable:
  - header row, body rows и column count из `mapTable`;
  - header cells визуально bold;
  - alignment из delimiter row применяется к paragraph format ячейки;
  - cell inline spans отображаются по существующим правилам (emphasis,
    links/images inert, code/HTML protected).
- Cell text editing:
  - normal mapped text run внутри ячейки редактируется существующим ReplaceText;
  - punctuation and `|` escaping и full AST guard остаются engine contract;
  - cross-run, Enter/newline, empty-run и unsupported fragments отклоняются.
- Unsupported table (nested, ragged, failed map, malformed) остаётся opaque
  placeholder с diagnostic tooltip.
- Table context для blockquote/list actions остаётся недоступным, если engine
  quote/list contract не поддерживает table block; actions disabled.
- Qt Test: rendered table structure/header/alignment, cell text edit, undo,
  mode switch, source preservation, unsupported fallback.

**Не включено:**

- Создание таблиц, insert/remove row/column, change alignment, remove table
  через continuous UI.
- Редактирование пустых cells, добавление содержимого в пустую cell,
  multi-block cells, nested tables и tables внутри list/quote.
- Форматирование table cells, merges, column resize, borders/themes beyond
  базового QTextTable style.
- Изменение Markdown table serializer или definitions policy.
- Syntax highlighting, external dependencies, HTML/PDF table redesign.

## Ограничения и предположения

- Поддерживаются только top-level tables с успешным `mapTable`; nested tables
  рендерятся как placeholder до отдельного среза.
- QTextTable является projection, не canonical source. Source меняется только
  через ReplaceText для существующего mapped normal text run.
- Заголовок bold и alignment — presentation; они не меняют Markdown.
- Пустая cell не имеет mapped run и остаётся read-only; для добавления текста
  требуется Markdown или будущая cell-insertion команда.
- Cell text с `|`, `*` и другой Markdown punctuation редактируется через
  существующий escape/guard path; при риске structural change правка
  отвергается.
- Table source bytes, delimiter row, column count и definitions не меняются
  при cell text edit.
- QTextTable не должен становиться активным HTML/browser редактором; raw HTML
  и links остаются inert.

## Критерии приёмки

1. Top-level table с header/body отображается как QTextTable с правильным
   числом строк/колонок; header bold, alignment соответствует delimiter row.
2. Existing normal cell text редактируется через continuous UI; source
   меняется только в cell text, delimiter row и таблица сохраняются.
3. Cell edit с `|`/Markdown punctuation не ломает таблицу; опасная structural
   правка отвергается и projection восстанавливается.
4. Cross-run/newline/empty-run edits отклоняются; links/images/code/HTML
   внутри cells остаются inert/protected по существующим правилам.
5. Nested/ragged/failed-map table остаётся opaque placeholder без изменения
   source; blockquote/list actions для table не включаются.
6. Undo/redo, dirty/token, mode switch и save contract сохраняются.
7. Engine/GUI tests покрывают success/rejection; Windows/Linux CTest и Linux
   sanitizers проходят.

## План проверок

- GUI fixtures: render 2×3 table, header/alignment, edit cell text, pipe
  escaping, undo/redo, mode switch, nested/ragged fallback, empty-cell
  protected.
- Engine regression: existing table text edit guard and table commands remain
  unchanged.
- Windows UCRT64 Debug: build, CTest, MD4C spike.
- Ubuntu WSL: normal и ASan/UBSan/leaks CTest.
- `git diff --check --ignore-space-at-eol`.
- Native visual QA, clang-format и remote CI отдельно; если недоступны,
  указать явно.

## Зависимости и открытые вопросы

- Зависимости: table map/commands 021–024, continuous slices 026–032.
- Открытых блокирующих вопросов нет. Structural table UI и empty-cell editing
  отложены.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.

## Исполнительский промпт

Реализуй только первый visual/read-write cell slice. Используй `mapTable` и
существующий `renderInline` для cell content; создавай QTextTable только для
top-level mapped tables. Не добавляй table creation/structure commands, не
сериализуй таблицу заново и не меняй delimiter/definitions. Cell text должен
идти через существующий ReplaceText и полный AST guard. Unsupported tables —
opaque placeholder. Проверь Windows/Linux tests и sanitizers; отчёт добавь сюда.

## Итог выполнения

### Реализация

- `ContinuousEditor::renderTable` строит QTextTable для top-level table с
  успешным `mapTable`: header + body rows, column count, header background/bold,
  alignment из delimiter row и cell padding/border.
- Cell content рендерится существующим `renderInline`; normal mapped text runs
  попадают в `runs_` и редактируются прежним ReplaceText. Source меняется
  только в cell text; delimiter row, column count, definitions и байты вне
  таблицы сохраняются.
- `|` и другая Markdown punctuation в cell edit проходят существующий
  escape/guard path и не ломают таблицу. Cross-run/newline/empty-run edits
  отклоняются по прежним правилам.
- Nested/ragged/неуспешный `mapTable` остаётся opaque placeholder; table
  context не включается, поэтому Blockquote/List actions для table disabled.
- Добавлены GUI tests: QTextTable dimensions, header bold, left/center/right
  alignment, cell text edit, `|` escaping, undo, mode switch, pre/post source
  preservation и nested-table fallback.

### Проверки

- Windows UCRT64 Debug: build, **CTest 7/7 passed**; MD4C Qt Test **266
  passed**; GUI smoke **79 passed**.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build with the MD4C spike,
  CTest **7/7 passed**.
- Ubuntu sanitized Debug: ASan/UBSan/leaks with the MD4C spike, CTest **7/7
  passed**.
- `git diff --check --ignore-space-at-eol` прошёл. Native visual QA,
  clang-format, clang-tidy и remote CI не запускались.

### Ограничения

- Поддерживаются только top-level tables; nested tables и tables внутри
  list/quote остаются placeholder.
- Пустые cells не имеют mapped run: добавить в них текст через continuous UI
  нельзя.
- Insert/remove row/column, change alignment, remove table и table creation
  через UI не добавлены.
- Table cell inline formatting commands не открыты; QTextTable styling
  остаётся базовым.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.
- Создан локальный коммит; push не выполнялся.
