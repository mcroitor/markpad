# Task 017 — Базовые команды top-level списков

## Постановка архитектора

- Статус: `Draft`. Родитель: [014](task_014.md).
- Зависимости: [016](task_016.md), [025](task_025.md).
- Блокер Ready: включить согласованную политику definitions из 025 в payload
  contract и mandatory fixtures. Не начинать mutations до её согласования.
- Цель: InsertListItem, RemoveListItem, SetListKind, SetItemTask для top-level
  списков, покрытых 016. Не indent/outdent, не nesting/quotes.
- Контракт: [014 v2](task_014.md); код только `experiments/md4c` и итог карточки.

## Обязательные проверки без definitions

| Source / команда | Ожидаемый исход |
|---|---|
| `"3. a\n9. b\n"` / insert `c` в position 1 | Success: `"3. a\n4. c\n5. b\n"`, start 3, tight true |
| `"3. a\n9. b\n"` / SetListKind ordered start 3 | No-op: исходные bytes/revision |
| `"- a\n"` / remove LI | Success: пустой source |
| `"999999999. a\n"` / insert `b` | Overflow, нет нового документа |
| `"- a\n- b\n"` / insert `"p\n\nq"` в position 1 | Success: loose list; новый LI содержит два paragraphs, порядок a/p/q/b |
| `"- a\n\n- b\n"` / remove второй LI | Success: остаётся один LI с одним P, tight true по явному исключению 014 v2 |
| `"- a\n"` / task none → unchecked → checked → none | Success на каждом переходе; literal `a` неизменён |
| `"- a\n"` / SetItemTask none | No-op |
| `"3. a\n9. b\n"` / добавить task первому LI | Success: номер второго LI остаётся 9, task change не перенумеровывает |
| `"- a\n"` / insert `"[ ] literal"` | Success: новый LI не task, literal `[ ] literal` |
| Любой list / insert `""`, `" \n"`, definition-only | Invalid fragment |
| LI начинается с heading/code / добавить task | Unsupported |

Дополнительно UL ↔ OL с сохранением task-state, multiline/fenced code формы 016,
UTF-8/CRLF/EOF и byte-exact prefix/suffix. Для каждой команды invalid node,
index, foreign/stale/ABA, no-op где применим, revision overflow.
Expected tree включает независимые kind/start/tightness/task assertions.

## Исполнительский промпт v1

Не начинай до Ready и Done обеих зависимостей. Реализуй только четыре команды
этой карточки через существующий applyEdit. Начальный fragment с task-like
текстом не должен неожиданно стать task; definitions обрабатывай исключительно
по принятому решению 025, не по изолированному parse без окружения. Mandatory
fixtures не заменять отказами. Схема вывода и команды проверки —
[общий порядок](README.md#исполнение-дочерних-md4c-задач).
Fixtures не исполнять; UI/dependencies не добавлять. При блокере остановись.

## Итог

Не реализовано. Постановка ждёт definitions policy; тесты не запускались.
