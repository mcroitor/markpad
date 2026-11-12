# Task 046 — WYSIWYG block presentation: backgrounds, code blocks, table fit

## Статус и владелец

- Статус: `Done`.
- Владелец: архитектор/координатор; `cpp-developer`, `test-writer`, при необходимости
  `frontend-designer`.
- Приоритет: P1 (release blocker по owner QA).
- Источник: [release checklist, Continuous WYSIWYG](../release_checklist.md#continuous-wysiwyg)
  и [Paged WYSIWYG](../release_checklist.md#paged-wysiwyg).

## Контекст

В continuous и paged WYSIWYG:

- обычный editable text отображается на светло-голубом фоне, что пользователь
  считает лишним и отвлекающим;
- code block не воспринимается как единый визуальный блок: между строками
  видны белые полосы;
- таблица шире viewport и обрезается справа; горизонтальной прокрутки нет,
  полная таблица не видна.

Те же дефекты должны быть исправлены в paged WYSIWYG.

## Цель

Сделать WYSIWYG-блоки визуально чистыми и предсказуемыми: обычный текст на
базовом фоне темы, code block с цельным background, таблица полностью видна
или доступна через явную горизонтальную прокрутку.

## Область работ

**Включено:**

- Убрать постоянный светло-голубой background у editable text runs; сохранить
  ясную индикацию редактируемости без нарушения читаемости.
- Отрисовать code block как единый визуальный блок с цельным background на всех
  строках, включая blank lines; сохранить редактирование тела code block.
- Обеспечить fit таблицы в viewport (пересчёт ширины/колонок) либо явную
  горизонтальную прокрутку; не обрезать содержимое.
- Применить изменения к continuous и paged WYSIWYG через общий renderer.
- Обновить light/dark screenshots, README и тесты.

**Не включено:**

- Изменение GFM-модели, source bytes или табличных команд.
- Замена QTextEdit/QTextDocument на другой движок.
- PDF parity — Task 047.

## Ограничения и предположения

- Protected/read-only fragments должны оставаться отличимыми от editable text.
- Code block должен оставаться редактируемым; нельзя маскировать его read-only
  HTML или изображением.
- Таблица не должна терять ячейки; допускается горизонтальная прокрутка, если
  fit невозможен без искажения.
- Изменения должны быть одинаковы в continuous и paged; paged page-sheet
  реализация — Task 044.

## Критерии приёмки

1. Обычный editable text использует фон Base темы; light blue background
   отсутствует по умолчанию.
2. Code block выглядит единым блоком без белых межстрочных полос в light и dark
   theme; blank lines сохраняют background.
3. Широкая таблица полностью видна или доступна через горизонтальную прокрутку;
   содержимое не обрезается.
4. Поведение одинаково в continuous и paged WYSIWYG.
5. Source/token/undo/redo не меняются; тесты покрывают форматы/размеры и
   сохранение source.

## План проверок

- GUI tests для background форматирования, code block и ширины таблицы.
- Визуальные screenshots light/dark continuous и paged.
- Windows Debug/Release и Ubuntu Debug/Release/ASan CTest.

## Зависимости и открытые вопросы

- Зависимости: Tasks 005, 006, 043; финальная проверка paged — после Task 044.
- Открытый вопрос: нужна ли отдельная тонкая граница/подсветка editable runs
  вместо фона; согласовать визуальный стиль с owner sign-off.

## Исполнительский промпт

Исправь общий rich-text projection: убери постоянный editable background,
сделай code block цельным в light/dark, обеспечь fit или явную прокрутку
таблиц. Не меняй Markdown source/token/history. Покрой найденные случаи
GUI-тестами и screenshots; если fit таблицы требует изменения layout-контракта,
остановись и опиши выбор.

## Итог выполнения

- Убран постоянный light-blue background у editable text; protected fragments
  остаются визуально отличимыми.
- Code block получил block-level background и нулевые межстрочные margins;
  blank lines и строки кода теперь образуют единый блок в light/dark.
- Таблицам заданы процентные column width constraints; continuous mode
  использует ширину viewport, добавлен horizontal scrollbar как fallback.
  Широкая 4-column fixture полностью видна.
- `continuousDarkThemeColors` проверяет отсутствие постоянного фона и наличие
  block background у code; table test проверяет column constraints.
- Windows Debug/Release CTest 7/7. Evidence: обновлённые light/dark screenshots и
  `docs/qa/task_011/table-wide/continuous.png`.
- Ветка `feature/task_046`; paged page-sheet layout остаётся Task 044.
