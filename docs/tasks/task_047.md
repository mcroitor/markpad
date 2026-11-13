# Task 047 — PDF export layout parity

## Статус и владелец

- Статус: `Done`; владелец: архитектор/координатор, исполнитель: C++/Qt.
- Контракт: [Task 051](task_051.md).

## Контекст

Текущий PDF использует отдельный `QTextDocument::print` путь; page count и
разрывы расходятся с paged WYSIWYG (QA fixture 3 vs 2). Контракт 051 требует
общий `PagePlan` и явную ошибку при oversized-блоке вместо обрезания.

## Цель

Сделать PDF и печать эквивалентными общему page plan по размеру, полям,
порядку блоков и разрывам; устранить скрытое расхождение.

## Область работ

**Включено:** рендер PDF по `PagePlan`, page size/margins/typography, явная
ошибка overflow, атомарная запись, тесты page count/order/size, обновление
README/release checklist. **Не включено:** новый PDF SDK, изменение GFM,
физическая печать как release-проверка, альтернативная пагинация.

## Ограничения и предположения

- PDF обязан использовать тот же `PagePlan`, что canvas 054; `QTextDocument::print`
  с автоматической пагинацией запрещён.
- Unit не пересекает границу; при overflow экспорт завершается ошибкой и не
  создаёт/не перезаписывает целевой файл.
- Pixel-perfect растеризация не требуется; требуется структурная эквивалентность
  для документов без overflow.
- Ошибки записи и отмены печати не маскируются.

## Критерии приёмки

1. Для fixtures без overflow page size, margins, порядок units и page count
   совпадают с paged canvas/PagePlan.
2. Typography (body/heading/code/quote) соответствует настройкам и Task 046.
3. При oversized/wide unit экспорт возвращает явную ошибку с указанием блока;
   файл не создаётся и существующий не повреждается.
4. Нет потери/дублирования блоков, таблиц, code, ссылок и изображений; raw HTML
   остаётся безопасным.
5. Тесты покрывают count/size/order/typography и overflow; evidence обновлено.

## План проверок

- PDF tests на фиксированных fixtures; структурная проверка page count/текста.
- Сравнение с canvas/PagePlan; Windows/Linux Debug/Release CTest.
- Визуальная проверка PDF и отчёта об ошибке overflow; непроведённые проверки
  указать явно.

## Зависимости и открытые вопросы

051 Done, 052 Done, 054 Done. Физическая печать и native visual QA остаются
внешними проверками Task 011/055.

## Исполнительский промпт

Реализуй PDF/печать по `PagePlan` и контракту 051. Не используй автоматическую
пагинацию Qt и не вводи второй layout. Обеспечь явный отказ при overflow без
повреждения файла. Покрой тестами parity и ошибки, обнови документацию; при
невозможности структурного совпадения остановись с точным diff.

## Итог выполнения

Реализовано:

- PDF и печать рисуют `PagePlan` вручную через `QPainter`: каждая страница
  клипается по content rect, unit не разрезается. `QTextDocument::print` с
  автоматической пагинацией больше не используется.
- `exportPdfFromDocument`/`printDocumentFromDocument` клонируют WYSIWYG-проекцию,
  поэтому UI-документ, cursor и formatting не меняются. Приложение экспортирует
  именно projection из `ContinuousEditor`, а не отдельный HTML-layout.
- Layout использует logical 96 DPI для измерений и затем масштабируется в
  реальную страницу PDF; это сохраняет переносы строк и page breaks эквивалентными
  canvas. Для standalone `markdown::Document` остаётся HTML-путь.
- Overflow (высота/ширина unit) возвращает явную ошибку `PDF export refused...`
  и не создаёт/не перезаписывает целевой файл; печать также отказывает.
- Обновлены README, ARCHITECTURE и release checklist.

Проверки: Windows UCRT64 Debug и Ubuntu WSL GCC 15.2 Debug — полный CTest 8/8.
`markpad_pdf_export`: A4/Letter MediaBox, фактическое число PDF-страниц равно
`pageCount`, overflow сохраняет существующий файл, print отказывает. Smoke:
paged canvas и PDF дают одинаковые 5 страниц на fixture из 140 абзацев.
Внешняя проверка MiKTeX `pdfinfo`/`pdftotext`: A4 595x842, 5 страниц, извлекаемый
текст присутствует. Native visual PDF review и реальная печать не выполнялись;
pixel-perfect не заявлен, структурная parity на проверенных fixtures достигнута.
