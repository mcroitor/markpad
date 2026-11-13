# Task 052 — Общая измеряемая компоновка страниц

## Статус и владелец

- Статус: `Done`; владелец: архитектор/координатор, исполнитель: C++/Qt.
- Контракт: [Task 051](task_051.md).

## Контекст

Task 044 добавляет отступы блокам и рисует листы поверх потока. Фактическая
геометрия может расходиться с планом, а oversized-блок перекрывает следующие
листы. Контракт 051 требует единый `PagePlan`, который используют canvas и PDF.

## Цель

Реализовать детерминированный измеряемый план страниц для одного QTextDocument.

## Область работ

**Включено:** измерение units по QTextDocument layout, page/content geometry,
`documentTop`, разрывы только между units, atomic tables/fenced code, overflow
и warning, API и unit tests. **Не включено:** UI, PDF, изменение GFM и
форматирования документа.

## Ограничения и предположения

- Пагинация — view/export metadata; запрещены spacer blocks, изменения block
  margins или source bytes.
- Использовать фактический layout, а не сумму предполагаемых высот.
- Unit не делится; overflow не обрезается и не маскируется.
- Геометрия и DPI — по контракту 051; PDF использует points без экранного scale.

## Критерии приёмки

1. `PagePlan` содержит page size/content rect, pages с `documentTop` и
   first/last block, units с page/height/overflow, общий overflow/warning.
2. Порядок units и разрывы детерминированы; ни один unit не пересекает
   границу страницы, кроме собственного overflow.
3. Tables и fenced code остаются целыми; oversized unit помещается на отдельной
   странице с overflow, следующий unit не перекрывается в плане.
4. Покрыты A4/Letter/Legal, orientation, margins, несколько страниц, таблицы,
   код, большой block, некорректные margins и разные scale/DPI.
5. Source/token/документ не изменяются при построении плана.

## План проверок

- Unit tests на фактические `documentTop`/размеры и независимые fixtures.
- Проверка детерминизма и отсутствия изменения QTextDocument.
- Сборка и CTest Windows/Linux; санитайзеры где доступны.

## Зависимости и открытые вопросы

051 Done. API не должен зависеть от UI; при необходимости уточнить единицы —
только через контракт 051.

## Исполнительский промпт

Реализуй `PagePlan` строго по 051 в layout-модуле и тестах. Не меняй документ и
не добавляй второй механизм пагинации. Проверь фактическую геометрию, oversized
и atomic units. Отчитайся файлами, командами, результатами, ограничениями и
невыполненными проверками; при конфликте контракта остановись.

## Итог выполнения

Реализовано в `src/page_layout.{h,cc}`:

- `PagePlanner::plan` возвращает `PagePlan` с page/content geometry, `pages`
  (`documentTop`, first/last block, overflow) и `units` (first/last block, page,
  `documentTop`, height, overflow), общим warning.
- Units группируются по таблицам и `kPageUnitProperty`; разрывы только между
  units; переполнение по высоте и ширине помечается без изменения документа.
- `PageLayoutEngine::layout` теперь делегирует `PagePlanner::plan`, поэтому
  старые потребители и tests используют тот же план; второй пагинатор не
  добавлен.
- Планировщик не вызывает `setTextWidth`, `setDocumentMargin` или форматирование
  блоков; это покрыто отдельным тестом.

Проверки: `markpad_layout_tests` — 16 passed, включая новые `pagePlanGeometryAndBreaks`,
`pagePlanDoesNotMutateDocument`, `pagePlanAtomicUnitsAndOverflow`,
`legacyLayoutMatchesPlan`. Windows UCRT64 Debug: полный CTest 7/7. Ubuntu WSL
GCC 15.2 Debug: полный CTest 7/7. Санитайзеры и clang-format не запускались
(инструмент форматирования не найден в проверенных окружениях). UI/PDF по
контракту не менялись — это задачи 053/054/047.

Ограничение: план измеряет документ в его текущей ширине; canvas обязан
установить content width до вызова плана. Это зафиксировано для 053.
