# Task 005 — Непрерывный WYSIWYG-редактор

## Статус и владелец

- Статус: `In Progress` (родитель; реализованные срезы — Tasks 026, 027, 028, 029, 030)
- Владелец: архитектор + C++ разработчик
- Приоритет: P1

## Контекст

WYSIWYG continuous — полноценный редактируемый режим того же GFM-документа без визуальных границ страниц. Переключение режимов сохраняет общую модель и несохранённые изменения. GFM преобразователь и document model задаются Tasks 002–003.

## Цель

Реализовать редактируемое форматированное представление, которое преобразует правки обратно в GFM с сохранением поддерживаемой семантики.

## Область работ

**Включено:** режим switch, редактируемый document view, выбор/курсор, базовые GFM блоки и inline formatting из sketch, сериализация изменений в общую модель, undo/redo integration, режимный round-trip tests.

**Не включено:** page layout, экспорт, форматирование, невыразимое в GFM.

## Ограничения и предположения

- Не использовать HTML preview/browser как редактор без достоверного редактируемого round-trip.
- Не допускать тихого удаления неизвестных GFM узлов; в первом срезе показывать
  их как явно защищённые fragments/placeholders, сохраняя source bytes.
- Неизменённые source bytes сохраняются; глобальная пересериализация через Qt
  запрещена. Изменённый диапазон нормализуется только проверенной MD4C-командой.
- Загрязнённый встроенный HTML/активный контент не выполняется.

## Критерии приёмки

1. Пользователь может редактировать заголовки, абзацы, bold/italic, strikethrough, списки, цитаты, code, links, images и tables, если кандидат GFM pipeline подтвердил поддержку.
2. Изменения сохраняются в общей модели как GFM и видны после переключения в Markdown режим.
3. Переключение режимов не теряет курсор/selection без объяснимого ограничения и не сбрасывает dirty state.
4. Тесты проверяют edit-serialize-parse semantics и неизвестные/неподдерживаемые конструкции.

## План проверок

- Unit/integration tests для каждого поддерживаемого GFM элемента и mixed blocks.
- Round-trip tests из Task 002.
- Ручная проверка переключения Markdown ↔ continuous WYSIWYG.
- Сборка и CTest Windows/Linux.

## Зависимости и открытые вопросы

- Зависимости: Tasks 002, 003, 004.
- Владелец утвердил поэтапный WYSIWYG: сначала безопасный редактируемый срез
  [Task 026](task_026.md). Неподдержанные участки явно неизменяемы, редактируются
  в Markdown. Полная Task 005 не считается Done по завершении первого среза.
- Остальные inline/структурные команды потребуют отдельных контрактов;
  списки/цитаты/таблицы продолжают зависеть от MD4C этапов 014–025.
- Для Task 027 владелец утвердил непустое выделение одного run, без будущего
  ввода; повторная команда снимает стиль только со всего простого span.

## Исполнительский промпт

Версия 2: выполняй поэтапно, начиная с утверждённого контракта Task 026.
Общая Session и source-preserving MD4C-команды обязательны. Нельзя считать
первый ограниченный срез полной реализацией всех критериев родителя.
Не реализуй пагинацию/экспорт или неподтверждённые форматирующие команды.

## Итог выполнения

- Реализована Task 026 на ветке `feature/task_005` от main `285b8b6`:
  mode switch, safe text edits, heading/task commands и общая история.
- Первый срез: Windows/Linux CTest 3/3, Linux ASan/UBSan 3/3; подробности
  и ограничения — в [отчёте Task 026](task_026.md#итог-выполнения).
- Добавлена [Task 027](task_027.md): selected Bold/Italic/Strikethrough,
  menu/shortcuts, simple unwrap и сохранение направления выделения.
- Добавлена [Task 028](task_028.md): реальные инертные link labels и локальное
  редактирование текста без изменения адресов/definitions. Создание/удаление
  ссылок и редактирование href/title не включены.
- Реализована [Task 029](task_029.md): inert images с реальными alt-подписями и
  локальным редактированием alt без изменения src/title/definitions и без
  загрузки ресурсов. Создание/удаление изображений не включено.
- Реализована [Task 030](task_030.md): контекстные Blockquote/Ordered/Unordered
  actions, WrapInList для top-level paragraph и SetListKind для существующих
  списков. Multi-block selection, list removal и list item operations остаются
  вне среза.
- Выполнена зависимая подготовка структурных команд:
  [Task 016](task_016.md) — exact source-map top-level списков/пунктов.
- Реализована [Task 017](task_017.md): source-preserving InsertListItem,
  RemoveListItem, SetListKind и SetItemTask, definition inventory/conflict/use
  checks и context-aware AST guard. UI-команды списков добавлены в Task 030.
- Реализована [Task 018](task_018.md): exact top-level quote ranges и
  WrapInQuote/UnwrapQuote для document parent. UI-команды цитат добавлены в
  Task 030.
- Реализована [Task 019](task_019.md): nested LI/QUOTE mapping и list/quote
  команды с virtual content/prefix rendering. UI-интеграция добавлена в Task
  030.
- Реализована [Task 020](task_020.md): атомарные IndentListItem/OutdentListItem
  для mapped list-контейнеров. UI-интеграции пока нет.
- В [Task 025](task_025.md) согласована policy definitions: atomic reject
  conflicts/changes to surviving bindings и отказ удаления used definition.
  Parser scope проверяется characterization-тестами; контракт 017 разблокирован,
  implementation inventory/команд не подменяется этими тестами.
- Полный объём ещё не выполнен; новые formatting commands требуют отдельных этапов.
