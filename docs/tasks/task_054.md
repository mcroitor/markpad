# Task 054 — Интеграция постраничного WYSIWYG

## Статус и владелец

- Статус: `Done`; владелец: архитектор/координатор, исполнитель: C++/Qt.
- Контракт: [Task 051](task_051.md).

## Контекст

Новый canvas (053) должен заменить текущий paged `PageTextEdit` в приложении,
сохранив continuous-возможности, единую сессию и историю.

## Цель

Подключить canvas к `ContinuousEditor`/MainWindow и удалить старый путь
постраничной отрисовки только после проверки эквивалентности.

## Область работ

**Включено:** переключение Markdown/continuous/paged, status `Page N of M` и
overflow warning, page setup, команды/mapping, undo/redo, общая история, темы,
удаление spacer/margin-пагинации Task 044, интеграционные тесты и документация.
**Не включено:** PDF/печать (047), изменение GFM и добавление новых
пользовательских возможностей вне paged-режима.

## Ограничения и предположения

- Source bytes, token, dirty/checkpoint и безопасные отказы сохраняются.
- Переключение режимов не сериализует и не меняет документ.
- При смене режима/настроек план должен быть актуален до отображения и экспорта.
- Нельзя оставлять два конкурирующих механизма пагинации в production.
- Поведение — только по контракту 051, без решений из spike, не вошедших в него.

## Критерии приёмки

1. Переходы Markdown/continuous/paged сохраняют source/token/dirty, cursor и
   выделение в границах контракта, включая undo/redo после reflow.
2. Paged-режим поддерживает существующие text/heading/list/quote/code/link/image
   alt/table/HR-команды; операции на второй странице и безопасные отказы
   проверены.
3. Status bar показывает актуальный page count/current page; overflow warning
   виден; page setup и темы применяются без перезапуска.
4. Старый spacer/margin-путь удалён или недоступен; второй механизм пагинации
   не остаётся.
5. README/ARCHITECTURE описывают фактическое поведение, включая accessibility
   и иные ограничения.

## План проверок

- Интеграционные Qt tests, полный CTest Windows/Linux, проверка source/security.
- Ручные сценарии переключения, правки на нескольких листах, undo, тем и
  настроек; непроведённые проверки — явно.

## Зависимости и открытые вопросы

051 Done, 052 Done, 053 Done. PDF parity не входит; контракт PDF — 047/051.

## Исполнительский промпт

Подключи canvas 053 и `PagePlan` 052 к существующему редактору/окну, сохранив
source, token, историю и все поддерживаемые операции. Удали старую
пагинацию после проверок. Не меняй PDF в этой задаче. Отчитайся файлами,
командами, результатами и регрессиями; при потере поведения или данных
остановись и передай blocker.

## Итог выполнения

Реализовано:

- `ContinuousEditor` содержит `PageCanvas`; в paged-режиме editor скрывается,
  canvas показывает тот же `QTextDocument`. Pagination не меняет source,
  token, undo/redo и не добавляет spacer/margin-блоки.
- Cursor и selection синхронизируются в обе стороны; `focusInput()` выбирает
  активную поверхность. Каждый canvas-edit проходит через существующий
  `contentsChange`/`textChanged` → versioned command pipeline.
- MainWindow подключает canvas к тем же context actions, undo/redo event filter,
  page count/current page и overflow warning. Старый paged-путь PageTextEdit
  больше не вызывается из UI (мёртвые методы остаются в классе).
- Обновлены README, ARCHITECTURE и Task 044 note.

Проверки: Windows UCRT64 Debug — полный CTest 8/8. Ubuntu WSL GCC 15.2 Debug —
полный CTest 8/8. Smoke additions: canvas видим и editor скрыт, страницы
отрисовываются, spacer margin отсутствует, правка на второй странице меняет
source, undo возвращает его, heading-команда через canvas работает, table cell
edit и возврат в continuous mode сохраняют документ.

Ограничения: системная OS IME и native visual QA не проверены (только offscreen
и Qt input events); `QAccessibleTextInterface` не реализован; copy/cut/paste
через canvas не покрыты отдельным тестом; старые paged-методы PageTextEdit не
удалены; PDF всё ещё использует независимый путь (Task 047).
