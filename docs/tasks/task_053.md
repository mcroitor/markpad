# Task 053 — Редактируемый page canvas

## Статус и владелец

- Статус: `Done`; владелец: архитектор/координатор, исполнитель: C++/Qt.
- Контракт: [Task 051](task_051.md).

## Контекст

Spike 050 подтвердил примитивы одного QTextDocument с собственным canvas:
ввод, выделение мышью через страницы, Qt IME events и отрисовка. Нужна
production-поверхность вместо рисования поверх QTextEdit.

## Цель

Реализовать редактируемый canvas листов, использующий `PagePlan` и один
`QTextDocument`.

## Область работ

**Включено:** отрисовка листов/полей/номеров и страниц, прокрутка, hit-test мыши,
cursor/selection, клавиатура, undo/redo через документ, copy/paste, IME commit и
preedit, расширение листа при overflow, инвалидация, базовый accessibility
(фокус/имя/клавиатура). **Не включено:** подключение к MainWindow/ContinuousEditor
(054), PDF (047), изменение GFM.

## Ограничения и предположения

- Один QTextDocument и один QTextCursor; нельзя создавать отдельные документы
  или истории на лист.
- Пагинация не меняет форматирование документа; позиции листов — из `PagePlan`.
- IME нельзя оставлять на скрытом QTextEdit; система должна получать корректные
  preedit/commit.
- Overflow по 051: лист расширяется, содержимое доступно, warning виден; не
  обрезать и не перекрывать следующий лист.
- Полный QAccessibleTextInterface не обещается; ограничение документируется.

## Критерии приёмки

1. Canvas рисует несколько листов, поля, границы и номера; текст не пересекает
   границу листа, кроме расширенного overflow-листа.
2. Клик/hit-test, ввод, удаление, undo/redo, copy/paste и выделение мышью и
   клавиатурой работают на первом и последующих листах; выделение может
   пересекать границу.
3. IME commit и preedit отображаются и не теряют текст; отмена композиции не
   меняет документ.
4. Пересчёт при изменении содержимого, размеров, настроек, типографики и DPI
   сохраняет валидный cursor/selection и не меняет source/token.
5. Oversized unit доступен, лист расширен, warning показан; следующий лист не
   перекрывается.
6. Нет загрузки внешних ресурсов и исполнения активного содержимого.

## План проверок

- GUI tests на ввод/выделение/IME на второй и третьей странице, undo, reflow,
  overflow, hit-test и copy/paste.
- Проверка фактических координат и pixels для листов; Windows/Linux offscreen и
  native visual QA где доступна.
- Ручная проверка системной IME и ограничений accessibility; иначе явно
  записать как непроверенное.

## Зависимости и открытые вопросы

051 Done, 052 Done (для фактического плана). Возможные уточнения по IME и
accessibility — только в рамках контракта 051.

## Исполнительский промпт

Реализуй canvas по контракту 051 и плану 052. Не подключай его к production UI
(это 054), не меняй source bytes и не создавай второй QTextDocument. Покрой
ввод, selection, IME, reflow и overflow. Отчитайся файлами, командами,
фактическими результатами и непроверенными сценариями; при риске потери данных
остановись.

## Итог выполнения

Реализовано в `src/page_canvas.{h,cc}` и `tests/page_canvas_test.cc`:

- Один `QTextDocument`, собственный `QAbstractScrollArea`, страницы из
  `PagePlan` 052; документ не получает spacer/margin-разметку.
- Отрисовка листов/полей/номеров, центрирование, прокрутка, hit-test мыши,
  выделение (включая пересечение страниц), двойной клик по слову.
- Клавиатура: перемещение с Shift, Home/End/Ctrl+Home/End, PageUp/Down,
  Backspace/Delete/Enter/Tab, undo/redo, select all, copy/cut/paste.
- IME: commit и preedit; preedit рисуется у курсора, `inputMethodQuery` отдаёт
  rect/selection/surrounding text. `setViewScale`, `setPageSetup` и изменение
  содержимого вызывают пересчёт; cursor/selection сохраняются по позиции.
- Oversized unit расширяет лист, `layoutWarning`/`hasOverflow` доступны хосту.
- Signals: `layoutChanged`, `currentPageChanged`, `layoutWarningChanged`.

Проверки: новый `markpad_page_canvas` — 6 passed (ввод на второй странице и undo,
выделение через страницы, IME preedit/commit, расширение oversized-листа,
смена margins с сохранением курсора). Windows UCRT64 Debug: полный CTest 8/8.
Ubuntu WSL GCC 15.2 Debug: полный CTest 8/8. Offscreen; native visual QA,
системная OS IME, accessibility и copy/paste через versioned-команды не
проверены. Canvas ещё **не подключён** к UI: mapping/история/статус — Task 054.
`QAccessibleTextInterface` не реализован; это ограничение для 054/055.
