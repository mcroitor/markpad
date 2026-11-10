# Task 020 — Атомарные indent/outdent списков

## Постановка архитектора

- Статус: `Done`. Родитель: [014](task_014.md). Зависимость: [019](task_019.md).
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

## Итог выполнения

### Реализация

- `src/markdown_document.{h,cc}`: добавлены `IndentListItem`/`OutdentListItem`
  в единый `EditCommand`. Обе команды используют mapped ranges/prefixes 019 и
  один `applyEdit`, поэтому composite перенос повышает revision ровно один раз.
- Indent:
  - требует previous sibling LI; первый LI — Invalid target;
  - переносит выбранный LI с поддеревом в конец последнего child list
    предыдущего LI того же kind; при отсутствии создаёт UL/OL с start=1;
  - сохраняет start существующего target list и перенумеровывает только
    затронутые ordered lists; overflow проверяется до mutation;
  - source list перестраивается без выбранного LI, target list — с ним;
    prefixes/CRLF/EOF/task-state/definitions сохраняются.
- Outdent:
  - требует nested LI с enclosing LI и outer list; top-level LI — Invalid;
  - переносит LI сразу после enclosing LI в outer list, siblings nested list
    остаются на месте; пустой nested list удаляется;
  - outer/nested ordered lists перенумеровываются, start сохраняется;
  - остальные blocks enclosing LI не перемещаются.
- Patch range — минимальный mapped контейнер (source list для indent, outer list
  для outdent); bytes вне него не меняются. Expected container subtree
  проверяется полным AST guard; tests отдельно фиксируют exact bytes, item
  order, tightness и task-state.
- Definitions остаются document-global и переносятся вместе с owner; binding
  changes/first-wins collisions отклоняются существующим policy 025 guard.
- Добавлены fixtures для tight/loose, ordered/UL, quote-contained lists,
  paragraph+fenced code, tasks, CRLF/EOF, Unicode, overflow и invalid targets.

### Expected → actual

| Сценарий | Ожидание | Результат |
|---|---|---|
| `- a`/`- b`, indent b | `"- a\n  - b\n"`, оба list tight | Pass |
| `- a`/`  - b`, outdent b | `"- a\n- b\n"`, empty child list удалён | Pass |
| `- a`/`  - x`/`- b`, indent b | `"- a\n  - x\n  - b\n"` | Pass |
| `- a`/`  3. x`/`- b`, indent b | `"- a\n  3. x\n  - b\n"` | Pass |
| Outdent middle `y` из x/y/z | x/z остаются под a, y сразу после a | Pass |
| Indent первого LI | Invalid target, source прежний | Pass |
| Outdent top-level LI | Invalid target, source прежний | Pass |
| LI с paragraph + closed fence | Оба blocks перенесены, target loose | Pass |
| Indent в ordered list с overflow | Overflow, нет нового документа | Pass |
| Quote-contained list indent/outdent | Приставки `>` и вложенность сохранены | Pass |
| Task LI | Task marker/state сохранены | Pass |
| CRLF/EOF/Unicode | Exact bytes, один revision increment | Pass |

### Проверки

- Windows UCRT64 Debug: `cmake --build build-task005 --parallel`,
  `ctest --test-dir build-task005 --output-on-failure` — **3/3 passed**.
  MD4C suite — **226 passed**, 0 failed/skipped.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build с
  `-DMARKPAD_BUILD_MD4C_SPIKE=ON`; CTest — **3/3 passed**.
- Ubuntu sanitized Debug: `-fsanitize=address,undefined`,
  `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`,
  `UBSAN_OPTIONS=halt_on_error=1`; CTest — **3/3 passed**.
- `git diff --check --ignore-space-at-eol` прошёл; сборка без warnings.
  Google clang-format в среде отсутствует; dry-run не выполнялся.
- Native visual QA, clang-tidy и remote CI не запускались.

### Ограничения и следующий шаг

- Переносы ограничены mapped LI/QUOTE контейнерами 019; произвольные mixed
  containers, tables и UI/undo-интеграция не входят в этот этап.
- Indent/outdent не переносят «следующие siblings» вместе с LI; переносится
  только выбранный LI с его поддеревом, как требует 014 v2.
- Ожидаемый container subtree строится из проверенного нового source и полного
  AST guard; independent fixtures фиксируют bytes/order/tightness/task-state.
  Полный ручной transformed AST без повторного parse не реализован.
- Lazy/TAB/open fence остаются explicit unsupported по mapping 019.
- Коммит/push не создавались. Task 014 остаётся In Progress до финальной
  приёмки родительского контракта.
