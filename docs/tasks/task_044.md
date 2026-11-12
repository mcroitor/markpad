# Task 044 — Настоящая редактируемая постраничная поверхность

## Статус и владелец

- Статус: `Draft`.
- Владелец: архитектор/координатор; `cpp-developer`, `test-writer`, при необходимости
  `frontend-designer`.
- Приоритет: P1 (release blocker после QA).

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
- Редактирование текста, заголовков, списков, code, links/images, tables и HR
  в постраничном режиме.
- Синхронизация source ranges, token, undo/redo, cursor/navigation и dirty state.
- Согласование page count с PDF-export там, где это допускает Task 008; различие
  Qt-layout/PDF документируется.
- Тесты разрывов, больших блоков, навигации и round-trip.

**Не включено:**

- Изменение GFM-модели или добавление page-break markup.
- WYSIWYG typography settings (Task 043).
- Печать на физическом принтере и CI-инфраструктура.

## Варианты архитектуры (нужно решение)

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
6. README/ARCHITECTURE и screenshots обновлены; PDF/paged divergence документирован.

## План проверок

- Unit tests для page partition и block placement.
- GUI integration tests для правок и навигации на нескольких страницах.
- Windows/Linux CTest + sanitizers.
- Визуальные screenshots на фиксированном fixture; сравнение page count с PDF.

## Зависимости и открытые вопросы

- Зависимости: Tasks 005, 006; Task 043 для итоговой типографики.
- Нужно выбрать вариант A или B. C/D не рекомендуются.
- Нужно определить, допустим ли промежуточный вариант A как release milestone
  или требуется только B.

## Исполнительский промпт

Не сформирован до выбора архитектуры страниц.

## Итог выполнения

Ожидает решения владельца. Код не изменялся.
