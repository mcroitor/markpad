# Task 023 — Колонки и явное удаление top-level таблицы

## Постановка архитектора

- Статус: `Done`. Родитель: [015](task_015.md). Зависимость: [022](task_022.md).
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

## Итог выполнения

### Реализация

- `src/markdown_document.{h,cc}`: добавлены `InsertTableColumn`,
  `RemoveTableColumn`, `RemoveTable` в общий `EditCommand`/`applyEdit`.
- InsertTableColumn принимает position `0..C`, один plain-text header, ровно
  N plain-text body cells и обязательный alignment. Payload проходит ту же
  validation/escaping policy, что 022; header/body/delimiter/alignment
  вставляются синхронно.
- RemoveTableColumn удаляет header cell, все body cells и alignment выбранной
  колонки; последняя колонка запрещена. Оставшиеся raw cell fragments
  сохраняются.
- RemoveTable удаляет только доказанный table range, не соседние blocks.
  Top-level единственная таблица даёт пустой source; table между paragraphs
  сохраняет разделители; слияние соседних lists/quotes отклоняется полным AST
  guard как structural mismatch.
- Все операции используют нормализованный serializer 022 и context-aware
  expected table parse; dimensions/row count проверяются до возврата.
- CRLF/EOF, existing code/emphasis/escaped pipes, stale/foreign/overflow и
  invalid payload/index покрыты тестами.

### Expected → actual

| Fixture | Expected | Actual |
|---|---|---|
| C=2,N=2 insert column positions 0/1/2 | C=3, синхронные header/body/delimiter/alignment | Pass |
| Header-only C=1 + bodyCells=[] | C=2,N=0 | Pass |
| N=2 + один bodyCell | Invalid payload | Pass |
| Remove column 0/1 | C=1, raw fragments оставшейся колонки сохранены | Pass |
| C=1 remove column 0 | Invalid operation: last column | Pass |
| Единственная table в файле / RemoveTable | Пустой source | Pass |
| `pre\n\n< table >\n\npost` / RemoveTable | `pre\n\n\npost\n`, внешние bytes прежние | Pass |
| Table между двумя UL / RemoveTable | Structural mismatch, нет документа | Pass |
| Existing escaped code/emphasis + column insert | Raw fragments сохранены | Pass |
| CRLF column insert | Exact CRLF bytes | Pass |

### Проверки

- Windows UCRT64 Debug: build, **CTest 3/3 passed**, MD4C suite **230 passed**,
  0 failed/skipped.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build, CTest **3/3 passed**.
- Ubuntu sanitized Debug: ASan/UBSan/leaks, CTest **3/3 passed**.
- `git diff --check --ignore-space-at-eol` прошёл; сборка без warnings.
  Google clang-format в среде отсутствует; dry-run не выполнялся.
- Native visual QA, clang-tidy и remote CI не запускались.

### Ограничения и следующий шаг

- Только top-level tables; nested tables в LI/QUOTE containers — Task 024.
- Existing cell inline formatting не редактируется payload-командами; новые
  cells — plain text после escaping.
- Удаление таблицы, приводящее к слиянию соседних контейнеров или к
  невыразимому parent, отклоняется, а не исправляется расширением patch range.
- Полный parse остаётся verification-компромиссом; performance не
  оптимизировалась.
- Коммит/push не создавались; Task 015 остаётся In Progress до 024.
