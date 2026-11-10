# Task 017 — Базовые команды top-level списков

## Постановка архитектора

- Статус: `Done`. Родитель: [014](task_014.md).
- Зависимости: [016](task_016.md), [025](task_025.md).
- Policy 025 v1 согласована, включена ниже. Ready не обходит Done зависимостей.
- Цель: InsertListItem, RemoveListItem, SetListKind, SetItemTask для top-level
  списков, покрытых 016. Не indent/outdent, не nesting/quotes.
- Контракт: [014 v2](task_014.md) + [025 v1](task_025.md);
  код `src/markdown_document.{h,cc}`, existing Qt Test
  `experiments/md4c/source_document_test.cc` и связанные документы; UI не менять.

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

## Definitions — обязательное дополнение 025 v1

Все строки матрицы 025 для insert/remove — обязательные fixtures 017,
не просто characterization parse. Exact expected sources/bindings заданы там.

- Unique definition + rendered paragraph/link — success. Пример insert после
  base: `- base\n- [new]: /new "N"\n\n  [x][new]\n`, x → /new, N.
- Внешняя `[outer]: /keep "K"` + insert `[x][outer]` — success с сохранением
  definition bytes и старых sibling uses; isolated fragment parse недостаточен.
- Existing label collision, normalized case/spacing/Unicode collision,
  duplicate definitions fragment (в том числе одинаковые href/title) —
  атомарный `definition conflict`.
- Добавление definition, разрешающей surviving unresolved reference —
  `binding conflict`, даже если source того reference вне patch неизменён.
- Definition-only fragment — `invalid fragment`.
- Remove LI с definition, используемой sibling/link/image/table cell —
  `definition in use`. То же с identical fallback definition.
  Remove owner unused definition — success, source-only syntax удаляется с LI.
- SetListKind/SetItemTask существующего списка с definition + bound reference —
  success с прежним binding и definition spelling; для task включения первый
  **rendered** block всё равно должен удовлетворять базовому контракту.
- Эти fixtures повторяются с CRLF/EOF/Unicode и full/collapsed/shortcut/image
  references. Source-only inventory не становится rendered AST child.
  Existing external duplicates сохраняются, если unrelated edit не меняет winner.

Для выполнения fixtures требуется расширить mapList 016: internal definition
ownership и external-reference context. Текущие explicit refusals 016 отражают
границы старого read-only mapper, не разрешают отказаться от mandatory success 017.
В expected AST учитывать resolved fragment в контексте документа и independently
заданные href/title/definition identity; не брать expected из финального output.

## Исполнительский промпт v2

Не начинай до Ready и Done обеих зависимостей. Реализуй только четыре команды
этой карточки через существующий applyEdit. Начальный fragment с task-like
текстом не должен неожиданно стать task; definitions обрабатывай исключительно
по принятому решению 025 v1: inventory всех definitions и surviving uses,
GFM-normalized uniqueness, context-aware expected tree и refusal used deletion.
Не rename/hoist definitions и не менять старые bindings. Mandatory
fixtures не заменять отказами. Схема вывода и команды проверки —
[общий порядок](README.md#исполнение-дочерних-md4c-задач).
Fixtures не исполнять; UI/dependencies не добавлять. При блокере остановись.

## Итог выполнения

### Реализация и API

- `src/markdown_document.{h,cc}`: добавлены `TaskState`, `InsertListItem`,
  `RemoveListItem`, `SetListKind`, `SetItemTask` в единый `EditCommand`/
  `applyEdit`. Новый параллельный редактор или serializer не вводился.
- `mapList` расширен source-only definition inventory и context-aware
  изолированным parse: external top-level definitions включаются в правильном
  порядке, внутренние definitions остаются частью проверяемого fragment.
  Existing success/rejection ranges 016 сохранены.
- Definition inventory сканирует canonical definition syntax, подтверждает
  изолированный parse и отсутствие покрытия rendered text; label equivalence
  проверяется pinned MD4C через probe (`Unicode case folding`, whitespace,
  multiline labels). Source-only definitions не становятся AST children.
- Insert строит новый LI из проверенного fragment, экранирует начальный
  task-like `[ ]`/`[x]`/`[X]`, сохраняет EOL/EOF, gap/loose/tightness и
  последовательную нумерацию ordered list с сохранением start.
- Remove удаляет LI/list, пересчитывает ordered номера, не переносит
  definitions; winning used definition даёт `Definition in use`, unused
  definition удаляется с owner. Collapsed/shortcut/full/image/table uses
  проверяются source-backed scanner-ом.
- SetListKind меняет UL/OL/start, сохраняет task-state, definitions и
  continuations; повторяющийся kind/start — no-op без нормализации.
- SetItemTask добавляет/снимает/меняет marker, не перенумеровывает список и
  требует plain text first rendered paragraph. Marker ставится по первому
  rendered block, а не слепо в начало source.
- Каждая команда после parse независимо проверяет ожидаемые item count/kind/
  start/tightness/task-state; затем полный `apply` сверяет AST с
  реконструированным list-subtree и сохраняет bytes вне patch range.

### Expected → actual

| Сценарий | Ожидание | Результат |
|---|---|---|
| Ordered insert `3/9` + `c` | `3. a\n4. c\n5. b\n`, start 3 | Pass |
| UL insert multiline `p\n\nq` | loose list, порядок a/p/q/b, два paragraph в новом LI | Pass |
| Insert definition + use | `- base\n- [new]: /new "N"\n\n  [x][new]\n` | Pass |
| External definition + `[x][outer]` | Success, binding `/keep`, title K | Pass |
| Task-like literal fragment | Новый LI не task, literal `[ ] literal` | Pass |
| Empty/space/definition-only fragment | `Invalid list item fragment` | Pass |
| Ordered overflow | `Ordered list number overflow`, нет документа | Pass |
| Definition collision (exact/case/space/fragment duplicate) | `Definition conflict`, source/token прежние | Pass |
| New definition resolves old unresolved ref | `Binding conflict`, source прежний | Pass |
| Remove only/middle/last LI, ordered renumber | Exact bytes, list disappears on last LI | Pass |
| Remove unused definition | Definition удалена с owner, binding sibling сохранён | Pass |
| Remove used definition (full/collapsed/shortcut/image/table/Unicode) | `Definition in use`, включая identical fallback | Pass |
| UL↔OL, task-state, multiline/fenced code, CRLF | Exact bytes, start/kind/task сохранены | Pass |
| Kind/start no-op | Bytes/revision/identity без изменений | Pass |
| Task none→unchecked→checked→none | Marker меняется, текст/номера прежние | Pass |
| External definition + task | First paragraph становится task, binding прежний | Pass |
| Stale/foreign/ABA token, invalid node/index, overflow | Единые ошибки без mutation | Pass |

### Проверки

- Windows UCRT64 Debug: `cmake --build build-task005 --target
  markpad_md4c_spike markpad_tests markpad_document_tests`,
  `ctest --test-dir build-task005 --output-on-failure` — **3/3 passed**.
  MD4C suite — **202 passed**, 0 failed/skipped; GUI/document suites прежние.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build в
  `/tmp/markpad-task017` с `-DMARKPAD_BUILD_MD4C_SPIKE=ON` через pinned MD4C
  source; CTest — **3/3 passed**.
- Ubuntu sanitized Debug `/tmp/markpad-task017-sanitize`: C/C++/link
  `-fsanitize=address,undefined`, `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`,
  `UBSAN_OPTIONS=halt_on_error=1`; CTest — **3/3 passed**.
- `git diff --check --ignore-space-at-eol` прошёл (рабочее дерево с CRLF).
  Google clang-format в текущей среде недоступен
  (нет бинарника ни в Windows PATH, ни в Ubuntu), поэтому dry-run не выполнялся.
- Native visual QA, clang-tidy и remote CI не запускались.

### Ограничения и следующий шаг

- UI-команды списков не добавлены: постановка 017 явно запрещала менять UI.
- Поддержаны canonical top-level lists, покрытые 016. Nested lists/quotes,
  indent/outdent и quote wrap/unwrap — Tasks 018–020.
- Definitions в неподдерживаемом внешнем context (например, quote) могут
  сделать mapping explicit unsupported; новые/используемые bindings не
  угадываются.
- Add/toggle task на LI, где definition стоит перед первым rendered paragraph,
  явно unsupported: GFM task marker не распознаётся после source-only
  definition, а переносить/hoist definition запрещено policy 025.
- Reference-use scan для удаления definitions покрывает canonical full/
  collapsed/shortcut/image/table forms в одной строке; multiline reference
  syntax не расширена. Сканер консервативен: похожий на reference текст внутри
  title/HTML может привести к явному `Definition in use`, но не к потере данных.
  Полный parse остаётся принятым компромиссом, performance не оптимизировалась.
- Ожидаемый list-subtree реконструируется из проверенного нового container source;
  независимые assertions покрывают count/kind/start/tightness/task и точные bytes,
  но полностью ручная сборка transformed AST без повторного parse не реализована.
- Коммит/push не создавались; полная Task 014 и Task 005 остаются In Progress.
