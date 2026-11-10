# Task 007 — HTML-экспорт

## Статус и владелец

- Статус: `Done`
- Владелец: C++ разработчик; архитектор проверяет безопасность
- Приоритет: P2

## Контекст

README требует HTML export. Документный диалект — GFM. Markdown/embedded HTML/links/images являются недоверенным вводом; скрипты и активный контент не должны выполняться. Внешние ресурсы нельзя загружать бесконтрольно.

## Цель

Экспортировать текущую модель документа в пригодный для просмотра HTML с явной ошибкой при сбое.

## Область работ

**Включено:** HTML serializer/export command, сохранение в выбранный путь, кодировка, безопасная обработка raw HTML и URI, ошибки экспорта и тесты.

**Не включено:** удалённые ресурсы, встроенный browser, PDF, пакетный экспорт.

## Ограничения и предположения

- Выводить безопасный HTML из утверждённой GFM-модели.
- Скрипты и активный контент удалять/экранировать согласно консервативной политике.
- Не скачивать внешние изображения/ресурсы во время экспорта.
- Документ не считается экспортированным до успешного завершения записи.

## Критерии приёмки

1. Экспорт содержит заголовки, текст, списки, таблицы, code, ссылки и изображения в пределах поддерживаемого GFM.
2. Raw HTML/URI не могут внедрить активный скрипт или автоматическую загрузку неразрешённого ресурса.
3. Ошибки парсинга/сериализации/записи сообщаются и не отображаются как успех.
4. Автотесты проверяют fixtures и небезопасные payloads; лицензия любых зависимостей подтверждена.

## План проверок

- Unit tests с GFM fixtures и небезопасными HTML/link/image cases.
- Integration test экспорта во временный файл и ошибки записи.
- Проверка файла на валидность/ожидаемую структуру и отсутствие опасного активного содержимого.

## Зависимости и открытые вопросы

- Зависимости: Tasks 002 и 003.
- Принятое безопасное default: не исполнять raw HTML и не загружать удалённые ресурсы. Если библиотека не умеет надёжно sanitize, raw HTML экранировать/исключить.

## Исполнительский промпт

Реализуй Task 007 на основе утверждённого GFM pipeline и общей Document Model. Экспортируй в HTML из безопасной структуры; никогда не исполняй raw HTML/скрипты и не загружай внешние ресурсы при экспорте. Если sanitizer библиотеки не даёт проверяемых гарантий, экранируй raw HTML согласно ограничениям. Проверь URI схемы, ссылки, изображения и ошибки записи. Добавь автоматические тесты для позитивных GFM fixtures и XSS-like input (как данные теста, не исполнять). Не добавляй отдельный browser или новую зависимость без согласования. Запусти целевые тесты и сообщи результаты, зависимости и ограничения.

## Итог выполнения

### Реализация

- Добавлены `src/html_export.{h,cc}`: безопасный HTML serializer по общему
  GFM `Document` AST. Поддержаны headings, paragraphs, blockquotes, UL/OL/task
  lists, code blocks/inline code, tables с alignment, emphasis/strong/del,
  links/images, autolinks, entities и soft/hard breaks.
- Raw HTML block и inline HTML не исполняются: весь raw fragment
  HTML-экранируется. Entities выводятся только при соответствии безопасному
  entity-pattern, иначе экранируются.
- URI policy: links разрешают `http`, `https`, `mailto` и безопасные relative
  URLs; images — `http`, `https`, relative без protocol-relative `//`;
  `javascript:`, `data:`, `file:`, `vbscript:`, прочие неизвестные схемы и
  control characters отклоняются. Небезопасная ссылка деградирует до текста,
  небезопасное изображение — до alt-текста. Внешние ресурсы не загружаются.
- Добавлен `src/qt_html_export_service.{h,cc}`: запись self-contained HTML в
  UTF-8 через `QSaveFile`, ошибки open/write/commit возвращаются явно.
- `MainWindow`: File → Export HTML… с выбором пути, парсингом текущего snapshot,
  безопасным rendering и сообщением об ошибке; успешная запись показывается в
  status bar.
- Новый target `markpad_export` и CTest `markpad_html_export` с GFM/XSS/URI/
  Unicode/table/file-error fixtures. Новых внешних зависимостей нет.

### Проверки

- Windows UCRT64 Debug: build, **CTest 4/4 passed**; `markpad_html_export`
  **7 passed**; MD4C suite **232 passed**, 0 failed/skipped.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build, CTest **4/4 passed**.
- Ubuntu sanitized Debug: ASan/UBSan/leaks, CTest **4/4 passed**.
- `git diff --check --ignore-space-at-eol` прошёл; сборка без warnings.
  Google clang-format в среде отсутствует; dry-run не выполнялся.
- Native visual QA, clang-tidy и remote CI не запускались.

### Ограничения

- Raw HTML намеренно экранируется, а не sanitize-ится до безопасного HTML;
  это консервативный default Task 007.
- Внешние images допускаются по безопасным URI, но экспорт сам их не
  скачивает; browser может запросить их при просмотре.
- PDF и paged export не входят в этот этап. UI export не проверялся native
  modal-dialog test; engine/file writer покрыты Qt Test.
- Использован общий full parse; performance не оптимизировалась.
- Коммит/push не создавались в рамках отчёта.
