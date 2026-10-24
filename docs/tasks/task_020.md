# Task 020 — Атомарные indent/outdent списков

## Постановка архитектора

- Статус: `Ready`. Родитель: [014](task_014.md). Зависимость: [019](task_019.md).
- Цель: две команды переноса LI с его поддеревом по [014 v2](task_014.md).
- Scope: контейнеры 019, сохранение start и перенумерация только затронутых lists.
  Не добавлять UI/undo или новые формы mapping. Код — `experiments/md4c`.

## Обязательные проверки

| Source / команда | Ожидаемый исход |
|---|---|
| `"- a\n- b\n"` / indent b | Success: `"- a\n  - b\n"`, оба list tight |
| `"- a\n  - b\n"` / outdent b | Success: `"- a\n- b\n"`, пустой child list удалён |
| `"- a\n  - x\n- b\n"` / indent b | Success: `"- a\n  - x\n  - b\n"` |
| `"- a\n  3. x\n- b\n"` / indent b | Success: новый child UL после OL, OL start 3 сохранён |
| `"- a\n  - x\n  - y\n  - z\n- b\n"` / outdent y | Success: x/z остаются под a, y сразу после a перед b |
| `"- a\n- b\n"` / indent a | Invalid target: нет предыдущего LI |
| `"- a\n"` / outdent a | Invalid target: list top-level |
| Перенос LI с paragraph + closed fence | Success: оба blocks перемещены вместе, target list loose |
| Перенос в ordered list с выходом номера за 999999999 | Overflow, нет нового документа |

Mandatory также quote-contained list из 019, task LI, UTF-8/CRLF/EOF,
source bytes вне общего предка неизменны, один revision increment на перенос.
Expected tree задаёт старый/новый parent, sibling order, tightness и task-state
до сериализации. Reparse сравнивается с ним, не только с текстом.

## Исполнительский промпт v1

Реализуй IndentListItem/OutdentListItem через один applyEdit, используя готовый
ownership 019 и точную семантику 014 v2. Выбери минимальный доказанный общий
контейнер; не перемещай следующих siblings вместе с выбранным LI. Нельзя менять
ожидаемое дерево по результату parse или превращать два изменения в две revision.
Все mandatory fixtures и предыдущие тесты обязательны. Проверки и отчёт —
[общий порядок](README.md#исполнение-дочерних-md4c-задач). Недоверенные fixtures
не исполнять, при блокере не объявлять Done/родительскую 014 Done.

## Итог

Не реализовано; проверки не запускались.
