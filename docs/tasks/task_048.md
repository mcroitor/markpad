# Task 048 — Toolbar actions and dropdown menus in Markdown mode

## Статус и владелец

- Статус: `Done`.
- Владелец: архитектор/координатор; `cpp-developer`, `test-writer`.
- Приоритет: P1 (release blocker по owner QA).
- Источник: [release checklist, Toolbar](../release_checklist.md#toolbar).

## Контекст

Пользователь сообщает, что в Markdown editor toolbar buttons не реагируют на
нажатия и не выполняют ожидаемые действия. Кнопки заголовков и Switch Mode не
открывают выпадающие меню. В continuous WYSIWYG эти же controls частично
работают, поэтому проблема может быть в mode-specific wiring или в
`QToolButton` popup/menu setup.

## Цель

Сделать toolbar предсказуемым во всех трёх режимах: кнопки выполняют
поддерживаемые действия, dropdown-кнопки открывают меню, недоступные действия
остаются видимыми, но disabled с объясняющим tooltip.

## Область работ

**Включено:**

- Воспроизвести toolbar clicks в Markdown, continuous и paged mode.
- Проверить `QAction`/`QToolButton` connections, popup mode/menu, enabled state
  для heading/switch-mode/list/table/link/image и inline actions.
- Реализовать Markdown-операции через source-preserving command pipeline для
  поддерживаемых действий; недоступные действия корректно disabled.
- Обеспечить открытие heading и switch-mode dropdown меню.
- Добавить GUI-тесты clicks/menu actions и состояние enabled/disabled.
- Обновить tooltips/README при изменении поведения.

**Не включено:**

- Новые GFM-конструкции вне существующего scope.
- Изменение toolbar visual design/icons без необходимости.
- Task 046/047 rendering/PDF fixes.

## Ограничения и предположения

- Toolbar actions должны менять source только через Session/command contract;
  нельзя обходить undo/redo и token guards.
- В Markdown mode действия должны работать с выделением/курсором исходника.
- Если действие не поддерживается в режиме, оно disabled и не выглядит
  сломанным.

## Критерии приёмки

1. В Markdown mode клики по поддерживаемым toolbar actions дают ожидаемое
   изменение Markdown source и корректный undo/redo.
2. Heading dropdown и Switch Mode dropdown открываются и выполняют выбранное
   действие; повторные клики не ломают focus/mode.
3. В continuous/paged mode actions применяются к rich editor; неподдерживаемые
   действия disabled с tooltip.
4. Source/token/dirty state остаются корректными во всех режимах.
5. GUI-тесты покрывают toolbar clicks и dropdown menus; screenshots обновлены.

## План проверок

- QTest GUI-тесты с fake dialogs для link/image/table.
- Проверка actions через QAction trigger и mouse/toolbutton.
- Windows Debug/Release и Ubuntu Debug/Release/ASan CTest.

## Зависимости и открытые вопросы

- Зависимости: Tasks 004, 032.
- Нужно уточнить у owner, какие именно кнопки не реагировали: все или только
  formatting/heading/switch mode; приложить шаги воспроизведения.

## Исполнительский промпт

Воспроизведи toolbar failure в Markdown mode, проверь connections/popup/menu
и enabled state. Реализуй source-preserving действия для поддерживаемых
команд, dropdown menus и корректные disabled tooltips. Покрой тестами; не
скрывай нереализованные действия включёнными.

## Итог выполнения

- Markdown toolbar actions теперь работают с исходником: bold/italic/strike
  wrap/toggle, heading/paragraph, blockquote, ordered/unordered list, code,
  horizontal rule, link, image и table.
- Неподдерживаемые в Markdown действия остаются disabled с tooltip; actions
  используют существующий source/history pipeline.
- Heading, List и Switch Mode toolbar buttons переведены в `InstantPopup`,
  поэтому dropdown открывается по клику.
- Добавлены GUI-тесты `markdownToolbarActions` и `markdownToolbarInsertions`:
  bold/heading/list, popup mode, link/table/code insertion.
- Windows Debug CTest 7/7; release build проверяется перед merge.
- Ветка `feature/task_048`; требуется remote CI/merge.
