# Task 031 — Редактируемые fenced code blocks в continuous WYSIWYG

## Статус и владелец

- Статус: `Done`.
- Владелец: архитектор + C++ разработчик.
- Родитель: [Task 005](task_005.md). Зависимости: 012, 026–030.

## Контекст

Continuous WYSIWYG показывает fenced code block как защищённый placeholder
`[Read-only: code - edit in Markdown]`. Engine умеет точно находить fenced
code range, но не имеет команды замены тела блока. В результате criterion 1
Task 005 для code остаётся невыполненным, хотя fences, info string и
окружающий Markdown можно сохранить без глобальной сериализации.

## Цель

Показывать тело fenced code block в continuous WYSIWYG и разрешить его
локальное редактирование с сохранением fence marker/count, info string,
EOL и всех байтов вне тела блока. Изменения должны проходить через общий
Session/history/undo contract и полный AST guard.

## Область работ

**Включено:**

- Новый read-only `mapFencedCodeBody(document, node)` для top-level fenced code:
  block range, body range, EOL, closed/unclosed сведения.
- Новый source-preserving `ReplaceCodeBody` engine command:
  - принимает node и UTF-8 body;
  - нормализует EOL к EOL блока;
  - сохраняет opening/closing fence, marker char/count, indentation и info
    string;
  - для closed block добавляет завершающий EOL перед closing fence, если его
    нет; для unclosed — не создаёт закрывающий fence;
  - отвергает invalid UTF-8, NUL, не-code node, nested/indented code и
    неоднозначный AST.
- Continuous rendering:
  - тело code block отображается monospace в защищённом frame;
  - изменения внутри code body отправляются как `ReplaceCodeBody`;
  - Enter, многострочный plain paste, удаление/вставка внутри code body
    поддерживаются в пределах fenced body;
  - fences/info остаются вне редактируемого диапазона.
- Engine и GUI Qt Test для successful edit, info/fence preservation, CRLF,
  unclosed fence, empty body, outside bytes, undo/redo, mode switch, stale token
  и rejection.

**Не включено:**

- Indented code blocks и code blocks внутри list/quote.
- Изменение info string, fence marker/count или удаление code block.
- Syntax highlighting, language services, rich clipboard и resource loading.
- Table/HTML/HR rendering.
- Точный source-cursor mapping для code body: cursor switch в Markdown остаётся
  approximate с пояснением, если не удастся доказать exact mapping.

## Ограничения и предположения

- Поддерживается только top-level fenced code (`MD_BLOCK_CODE`, parent root,
  marker ``` или ~~~, opening indent не более 3 пробелов).
- Тело отображается с LF-нормализацией; при записи EOL приводится к EOL
  исходного блока.
- Closing fence является частью protected source и не редактируется.
- Если новое тело содержит строку, которая закрывает fence раньше, полный
  AST guard отвергает правку; source остаётся прежним.
- `ReplaceCodeBody` не пересериализует соседние блоки и не меняет definitions.
- Нет fallback на прямую замену source или QTextDocument HTML serializer.

## Критерии приёмки

1. ` ```\nold\n```\n ` → ` ```\nnew\n```\n ` через `ReplaceCodeBody`; fence и
   все байты вне body range сохранены.
2. Info string, marker char/count, opening indentation и CRLF сохраняются.
3. Unclosed fence остаётся unclosed; closed fence получает EOL перед closing
   fence, если новое тело его не содержит.
4. Empty body допустим и даёт пустой code block без потери закрывающего fence.
5. Invalid UTF-8, NUL, non-code node, nested/indented code и early-closing body
   отвергаются без изменения Session.
6. Continuous UI показывает реальное тело, разрешает Enter/plain paste/multiline
   edits внутри code body, а undo/redo/dirty/token/mode switch остаются
   согласованными.
7. Engine и GUI tests покрывают success/rejection; Windows/Linux CTest и Linux
   sanitizers проходят.

## План проверок

- Engine fixtures: простой edit, info, CRLF, closing fence lengths, unclosed,
  empty body, соседние blocks, invalid/NUL, early closing, nested/indented,
  no-op/stale token.
- GUI fixtures: редактирование строки, добавление строки, многострочная
  вставка, undo/redo, mode switch, rejection с восстановлением projection.
- Windows UCRT64 Debug: build, CTest, MD4C spike.
- Ubuntu WSL: normal и ASan/UBSan/leaks CTest.
- `git diff --check --ignore-space-at-eol`.
- Native visual QA, clang-format и remote CI отдельно; если недоступны,
  указать явно.

## Зависимости и открытые вопросы

- Зависимости: Task 012/013 source maps, continuous 026–030.
- Открытых блокирующих вопросов нет. Indented/nested code и cursor mapping
  осознанно отложены.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.

## Исполнительский промпт

Реализуй только описанный срез. Сначала добавь public `mapFencedCodeBody` и
`ReplaceCodeBody` с fixtures, затем code-body rendering/editing в
`ContinuousEditor`. Используй существующие source maps и общий `apply` AST guard.
Не добавляй глобальную сериализацию, HTML serializer, syntax highlighting,
nested/indented code или новые зависимости. Каждый reject должен восстанавливать
projection и не менять Session. Проверь Windows/Linux CTest и sanitizers.
Отчёт о выполнении добавь сюда.

## Итог выполнения

### Реализация

- Добавлен public `mapFencedCodeBody`: top-level fenced code block возвращает
  full block range, body range, EOL и closed flag. Body исключает opening и
  closing fence lines; indented/nested code и не-fenced блоки явно
  отвергаются.
- Добавлен `ReplaceCodeBody`: принимает UTF-8 body, нормализует line endings к
  EOL блока, сохраняет fence marker/count, indentation, info string и все
  байты вне body. Для closed block добавляется EOL перед closing fence; для
  unclosed fence новый closing fence не создаётся. Пустое body допустимо.
- Structural guard парсит candidate code block, проверяет attributes и
  применяет общий `apply` к полному source. Invalid UTF-8/NUL, early-closing
  body и structural mismatch отвергаются без изменения Session.
- `ContinuousEditor` теперь рендерит тело top-level fenced code monospace в
  защищённом frame вместо placeholder. `CodeRun` mapping позволяет менять
  строки, добавлять строки, использовать Enter/plain paste и удалять тело;
  fences/info остаются вне редактируемого диапазона.
- Изменения code body идут через `commandRequested`/Session/history; reject
  восстанавливает projection и показывает ошибку. Format-команды внутри code
  body не открыты.
- Engine fixtures покрывают basic/info/CRLF/append EOL/empty/unclosed/outside/
  shorter fence/longer close/blank lines, no-op, stale token и rejection.
  GUI tests покрывают рендер тела, edit, add line, undo, early-closing reject
  и code blockquote.

### Проверки

- Windows UCRT64 Debug: build, **CTest 7/7 passed**; MD4C Qt Test **266
  passed**; GUI smoke **76 passed**.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build with the MD4C spike,
  CTest **7/7 passed**.
- Ubuntu sanitized Debug: ASan/UBSan/leaks with the MD4C spike, CTest **7/7
  passed**.
- `git diff --check --ignore-space-at-eol` прошёл. Native visual QA,
  clang-format, clang-tidy и remote CI не запускались.

### Ограничения

- Поддерживаются только top-level fenced code blocks; indented и nested
  list/quote code остаются за пределами среза.
- Info string, fence marker/count и сам code block нельзя изменить или удалить
  через WYSIWYG.
- Нет syntax highlighting, language services, rich clipboard или resource
  loading.
- Точный cursor mapping для code body при переключении в Markdown не
  реализован; protected/approximate mapping сохраняется.
- Code body с early-closing fence отвергается; пользователь должен изменить
  его в Markdown.
- Table/HTML/HR rendering не реализованы; полная Task 005 остаётся In Progress;
  Task 011 остаётся Blocked.
- Создан локальный коммит; push не выполнялся.
