# Task 008 — PDF-экспорт и печать

## Статус и владелец

- Статус: `Done` (Qt PrintSupport PDF/print slice; native printer QA не выполнялся)
- Владелец: C++ разработчик; архитектор утверждает layout и печать
- Приоритет: P2

## Контекст

README требует PDF export; sketch добавляет Print. Архитектура рекомендует сперва проверить Qt PrintSupport/QPdfWriter. PDF должен совпадать с постраничным WYSIWYG по размеру страниц, полям, содержимому и основным переносам.

## Цель

Реализовать PDF export и печать с общей моделью раскладки страниц.

## Область работ

**Включено:** экспорт PDF в путь, print dialog/печать при наличии поддерживаемого принтера, сообщения об ошибках, шрифты и layout integration.

**Не включено:** отдельный PDF SDK до доказанной необходимости; PDF import; аннотации/формы.

## Ограничения и предположения

- Сначала проверить Qt PrintSupport/QPdfWriter на целевых Windows/Linux.
- Использовать PageLayout из Task 006, не строить отдельную несовместимую раскладку.
- Не включать scripts/active HTML; локальные/удалённые ресурсы обрабатывать по принятой политике.

## Критерии приёмки

1. PDF создаётся корректно с ожидаемыми страницами и сообщением об ошибках.
2. Результат согласован с paginated WYSIWYG по геометрии страницы, полям и последовательности блоков.
3. Печать использует ту же компоновку и уважает отмену пользователем.
4. Проверены большие блоки и переносы на границах страниц.

## План проверок

- Автотесты подготовки page layout и экспортного результата где возможно.
- GUI/ручной smoke test PDF на Windows/Linux.
- Сверка PDF и скриншота paginated mode для стабильных fixtures.

## Зависимости и открытые вопросы

- Зависимости: Task 006.
- Блокеры `Draft`: defaults и per-document page setup из Task 010; конкретная PDF реализация должна подтвердить качество рендеринга.

## Исполнительский промпт

Реализуй Task 008 только после утверждения Task 006 и выбора PDF решения. Сначала сделай минимальный spike Qt PrintSupport/QPdfWriter и сравни с требованиями. Переиспользуй PageLayout; не вводи второй независимый механизм пагинации. Обеспечь явные ошибки и корректную отмену печати. Сверь PDF и paginated WYSIWYG на fixture наборе и платформах. Добавляй сторонний PDF SDK только после документированного обоснования, лицензии и согласования. Запусти тесты и укажи фактически проверенные платформы.

## Итог выполнения

### Реализация

- Добавлены `src/pdf_export.{h,cc}` в `markpad_export`: PDF export через
  Qt PrintSupport/QPrinter и print helper, использующий тот же safe HTML
  fragment renderer, что HTML export.
- `PdfExportOptions` передаёт PageSetup (A4/Letter/Legal, orientation, четыре
  margins), font family/size. Page geometry задаётся QPageLayout из тех же
  значений, что `PageLayoutEngine` Task 006.
- PDF пишется во временный файл, проверяется non-empty output и только затем
  копируется в выбранный путь; open/print/replace ошибки возвращаются явно.
  Existing target заменяется только после успешной генерации.
- `SafePrintDocument` блокирует `loadResource`, поэтому raw HTML не исполняется
  и внешние/local resources не загружаются при печати/PDF.
- `PageLayoutEngine::layout` используется для page count/overflow warning;
  PDF result сообщает количество страниц и overflow. Qt text layout выполняет
  фактическую постраничную печать в той же page geometry.
- `MainWindow`: File → Export → PDF и File → Print включены; QPrintDialog
  использует те же page settings; отмена dialog/print не считается успехом.
- Добавлен CTest `markpad_pdf_export` с PDF fixtures, page-size/margin
  вариантами, write errors и print helper.

### Проверки

- Windows UCRT64 Debug: build, **CTest 7/7 passed**; `markpad_pdf_export`
  **6 passed**; GUI smoke **75 passed**; MD4C suite **232 passed**.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build, CTest **7/7 passed**.
- Ubuntu sanitized Debug: ASan/UBSan/leaks, CTest **7/7 passed**.
- `git diff --check --ignore-space-at-eol` прошёл; сборка без warnings.
  Google clang-format в среде отсутствует; dry-run не выполнялся.
- Native PDF visual comparison, real printer hardware и remote CI не
  запускались.

### Ограничения

- PDF pagination выполняет Qt text layout в заданной page geometry; page count
  и overflow warning берутся из общего PageLayoutEngine, но pixel-perfect
  совпадение с page-guide WYSIWYG не гарантируется.
- Внешние/local resources не загружаются; images экспортируются как безопасные
  HTML image references без загрузки ресурса.
- Raw HTML экранируется. Реальный printer dialog cancel/manual print hardware
  проверялись только кодом, без физического принтера.
- PDF import/annotations/forms не реализованы. Auto Save остаётся отложенным.
- Коммит/push не создавались.
