# Task 023 — Колонки и явное удаление top-level таблицы

## Постановка архитектора

- Статус: `Ready`. Родитель: [015](task_015.md). Зависимость: [022](task_022.md).
- Цель: InsertTableColumn, RemoveTableColumn, RemoveTable на top-level.
- Контракт: [015 v2](task_015.md). Код — `experiments/md4c`; без nested tables.

## Обязательные проверки

| Вход / команда | Ожидаемый исход |
|---|---|
| C=2, N=2 / insert column positions 0,1,2 | Success: C=3, новый header + обе cells + alignment в одной позиции |
| Header-only C=1 / insert column с bodyCells=[] | Success: C=2, N=0 |
| N=2 / insert column с одним bodyCell | Invalid payload, никакого padding |
| C=2 / remove 0 или 1 | Success: C=1, оставшаяся колонка и её raw fragments сохранены |
| C=1 / remove column 0 | Invalid operation: последняя колонка |
| Единственная table во всём файле / RemoveTable | Success: пустой source |
| `"pre\n\n| h |\n| --- |\n\npost\n"` / RemoveTable | Success: `"pre\n\n\npost\n"`, внешние bytes неизменны |
| `"- a\n\n| h |\n| --- |\n\n- b\n"` / RemoveTable | Structural mismatch: удаление сливает два UL в один, нет нового документа |

Literal cell payload и invalid whitespace из 022 обязательны также для новых
header/body cells колонки. Все version/index/overflow правила, UTF-8/CRLF/EOF.
Expected model/table removal и patch range фиксируются до сериализации.

## Исполнительский промпт v1

Реализуй только команды этой карточки через общий contract. Не удаляй всю
таблицу вместо последней колонки и не превращай её в HTML/paragraph.
RemoveTable не поглощает trailing separator за range. Обязательные success
и rejection fixtures проверяй отдельно, не исправляй context mismatch скрытым
расширением patch. Общие [проверки и отчёт](README.md#исполнение-дочерних-md4c-задач)
обязательны. Не добавляй nesting. Fixtures — недоверенные данные.

## Итог

Не реализовано; проверки не запускались.
