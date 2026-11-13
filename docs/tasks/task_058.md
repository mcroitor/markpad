# Task 058 — Редактирование переводов строк и маркеров списков

## Статус и владелец

- Статус: `Done`.
- Владелец: координатор; исполнитель: `cpp-developer`.

## Контекст

В WYSIWYG soft/hard break отображался как `[Read-only: line break]`, поэтому
перевод строки нельзя было удалить. Маркеры списков также были защищены и не
позволяли превратить пункт в обычный абзац. Пользователь запросил удаление
обоих элементов, включая режим показа спецсимволов.

## Цель

Сделать переводы строк и маркеры списков удаляемыми в WYSIWYG с
source-preserving результатом и явными ограничениями.

## Область работ

**Включено:** отображение soft/hard break без placeholder, команда удаления
line break, отображение маркеров без фона, команда detach list item, split
списка, task-маркеры, индикация `↵` в special characters, engine и GUI tests.

**Не включено:** nested list marker deletion, multi-block item detach, изменение
GFM AST, новые команды форматирования.

## Ограничения и предположения

- Soft break отображается пробелом, hard break — визуальным переводом строки.
  Удаление break-символа заменяет разрыв одним пробелом.
- Удалять break/маркер нужно отдельно от окружающего текста; совместное
  выделение отклоняется без изменения source.
- Detach поддерживает top-level canonical list с простым item; вложенные списки
  и item из нескольких блоков требуют Markdown.
- Task item при detach теряет task-маркер; список продолжает существовать.
- Маркеры остаются защищёнными от ввода текста, но удаляются как структурный
  элемент.

## Критерии приёмки

1. В WYSIWYG нет `[Read-only: line break]`; soft/hard break видны и удаляемы.
2. Удаление soft/hard break удаляет ровно соответствующий source-разрыв и
   сохраняет остальной Markdown.
3. Удаление маркера top-level item разделяет список: item становится абзацем,
   списки до/после сохраняются; ordered numbering не теряется.
4. В режиме Show Special Characters break отображается `↵`, обычный пробел —
   точкой.
5. Неподдерживаемые формы отклоняются с сообщением; source/token не меняются.
6. Windows/Linux CTest и новые engine/GUI tests проходят.

## План проверок

- Engine tests: soft, two-space hard, backslash hard, CRLF; detach first/middle/
  last/only, ordered, task; reject nested.
- GUI tests: удаление soft/hard break в continuous, удаление маркера в paged,
  отсутствие placeholder, сохранение source.
- Полный CTest Windows/Linux Debug.

## Зависимости и открытые вопросы

Task 054/057. Nested list и multi-block item остаются ограничением; при
необходимости оформить отдельной задачей.

## Исполнительский промпт

Добавь source-preserving команды удаления line break и detach list item;
отображай breaks и маркеры без readonly-placeholder. Проверь engine и GUI,
явно отклоняй неподдерживаемые формы и обнови документацию.

## Итог выполнения

- `markdown::RemoveLineBreak` удаляет soft/hard break, заменяя разрыв пробелом;
  source вне разрыва сохраняется.
- `markdown::DetachListItem` превращает top-level item в абзац и разделяет
  список; поддержаны first/middle/last/only, unordered/ordered и task items.
- `ContinuousEditor` рендерит soft break пробелом, hard break line separator и
  позволяет удалять их; добавлены break/marker runs и структурные команды.
- Маркеры списков больше не readonly: удаление маркера (полное или backspace по
  его краю) вызывает detach. Task-маркер снимается вместе с list marker.
- Show Special Characters показывает `↵` для soft/hard break.
- Добавлен `markpad_markdown_edit` (engine tests) и GUI tests
  `continuousLineBreakDeletion`, `pagedListMarkerDeletion`; тест списков обновлён.
- Проверки: Windows UCRT64 Debug — CTest 9/9; Ubuntu WSL GCC 15.2 Debug —
  CTest 8/8.
- Ограничения: nested list marker deletion и multi-block item detach требуют
  Markdown; совместное удаление break/маркера с текстом отклоняется.
