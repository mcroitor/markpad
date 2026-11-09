# Task 019 — Mapping и базовые команды вложенных list/quote контейнеров

## Постановка архитектора

- Статус: `Done`. Родитель: [014](task_014.md).
- Зависимости: [017](task_017.md), [018](task_018.md).
- Цель: распространить mapping и уже реализованные команды на nested parents,
  не реализуя indent/outdent.
- Scope обязателен: list внутри LI, list внутри QUOTE, QUOTE внутри LI,
  цепочка QUOTE → LI → QUOTE. Space indentation, explicit quote prefixes,
  глубина 1–3; lazy/TAB/open fences — обязательный unsupported.
- Контракт [014 v2](task_014.md) + [025 v1](task_025.md);
  область кода — `src/markdown_document.{h,cc}` и existing MD4C Qt Test.

## Обязательные проверки

| Source / цель | Ожидаемый исход |
|---|---|
| `"- parent\n  - a\n  - b\n"` / insert c между a/b | Success: `"- parent\n  - a\n  - c\n  - b\n"` |
| `"> - a\n> - b\n"` / remove b | Success: `"> - a\n"` |
| `"- parent\n\n  > a\n"` / unwrap quote | Success: тот же LI с paragraphs parent и a, outer list loose |
| `"- parent\n\n  second\n"` / wrap second P | Success: `"- parent\n\n  > second\n"`, outer list остаётся loose |
| `"> - parent\n>\n>   > text\n"` / unwrap внутренней quote | Success: outer quote и LI сохранены; LI имеет два P, loose list |
| Nested ordered list / SetListKind и SetItemTask | Success с numbering/tightness правилами 014, внешний list не нормализуется |
| Quote/list siblings с повторяющимся literal | Success: выбрана именно адресованная структура |
| Единственный вложенный list как единственный block QUOTE / удалить последний LI | Unsupported: запрещено каскадно удалять quote |

Каждую базовую команду из 017/018 проверить хотя бы в одном nested parent
и с UTF-8/CRLF/EOF; bytes вне заранее заданного patch range неизменны.
Обязательно добавить reference-binding success/rejection fixtures policy 025 v1:
зависимость 017 должна уже содержать их. Самостоятельную новую политику не вводить.
Tightness expected вычислять до output parse, проверять implicit P normalization.

### Nested reference fixtures

- Source `"[outer]: /keep \"K\"\n\n> - a\n"` / insert `[x][outer]`:
  success `"[outer]: /keep \"K\"\n\n> - a\n> - [x][outer]\n"`;
  x → /keep, K; definition bytes прежние.
- Source `"> - a\n"` / insert `"[new]: /new \"N\"\n\n[x][new]"`:
  success `"> - a\n> - [new]: /new \"N\"\n>\n>   [x][new]\n"`;
  definition global, x → /new, N.
- Source `"> - [new]: /new \"N\"\n>\n>   owner\n> - [s][new]\n"` /
  remove owner LI: `definition in use`, source прежний; s → /new, N.
- Collision с definition внутри/вне outer quote/list и resolution старого
  unresolved reference — такие же atomic rejection, как в 017/025.
- Wrap/unwrap/indent-relevant mapping сохраняет spelling и definition identity
  без hoist/rename; source prefixes принадлежат parent context, не definition.
  Повторить depth 1–3 / CRLF / UTF-8 / EOF и reference images.

## Исполнительский промпт v2

Расширь существующие mapping/commands только на перечисленные nested формы.
Не дублируй сериализаторы и не делай nested поддержку глобальным rewrite документа.
Применяй document-global definition inventory/uses из 017 и policy 025 v1;
не создавай «локальный scope» для QUOTE/LI, не обходи conflicts переименованием.
Обязательные success fixtures нельзя отклонять как «нет доказанного mapping»:
если не получается, задача остаётся незавершённой с блокером. Negative fixtures
должны явно отказываться без изменения source. Используй
[общий порядок](README.md#исполнение-дочерних-md4c-задач), не добавляй indent/outdent.
Fixtures — недоверенные данные; report должен указывать реальные поддержанные
формы, не рекламировать общий lossless GFM pipeline.

## Итог выполнения

### Реализация

- `src/markdown_document.{h,cc}`: mapping top-level списков/цитат обобщён на
  nested parents LI и QUOTE. Добавлены `VirtualContent`/`VirtualParse`:
  контент родителя извлекается с удалением его line prefixes, парсится с
  document-global definitions context, а virtual ranges переводятся обратно в
  real offsets через line mapping. Рекурсия depth 1–3 поддерживается.
- `ListSource`/`QuoteSource` получили `continuationPrefix`/`blankLinePrefix`.
  Сериализаторы списков и item/task edits добавляют parent prefix к строкам
  после первой; canonical view для expected parse получается удалением этих
  prefixes. Top-level поведение (пустые prefixes) не изменилось.
- Insert/Remove/SetListKind/SetItemTask теперь работают и для nested list:
  сохраняются parent prefixes, ordered numbering, gaps/tightness и все bytes
  вне patch range. `mapItem` ищет item во всех mapped lists, а не только
  top-level.
- WrapInQuote/UnwrapQuote работают для document, QUOTE и LI parents.
  `mapNestedBlockRange` находит P/H, list, quote и fenced code внутри parent.
  Wrap добавляет ровно один `>`-уровень после parent prefix, unwrap снимает
  один уровень и сохраняет parent prefixes; ancestor LI tightness
  пересчитывается.
- Definitions остаются document-global: inventory/uses/conflicts из 017
  применяются к nested lists; external definitions вне quote/list включаются в
  virtual parse context. Source-only definitions не hoist/rename-ятся.
- Каскадное удаление запрещено: удаление последнего LI nested list, который
  является единственным block QUOTE, возвращает explicit Unsupported.
- Добавлены fixtures: insert/remove/kind/task в LI и QUOTE, wrap/unwrap P и
  nested quote, chain QUOTE→LI→QUOTE, duplicate nested lists с одинаковым
  literal, CRLF/EOF/Unicode, reference success/rejection, cascade rejection.

### Expected → actual

| Сценарий | Ожидание | Результат |
|---|---|---|
| Nested list insert между a/b | `"- parent\n  - a\n  - c\n  - b\n"` | Pass |
| Nested quote list remove b | `"> - a\n"` | Pass |
| Nested quote unwrap в LI | `"- parent\n\n  a\n"`, outer list loose | Pass |
| Wrap P в LI | `"- parent\n\n  > second\n"` | Pass |
| Chain QUOTE→LI→QUOTE unwrap | Внутренняя quote снята, outer quote/LI сохранены | Pass |
| Nested ordered insert/kind/task | Числа `3/4/5`, task marker, outer prefixes сохранены | Pass |
| Duplicate nested lists | Изменена именно адресованная структура | Pass |
| Nested loose list insert | Loose separator/prefixes сохранены | Pass |
| External definition + nested insert | `[x][outer]` → `/keep`, K | Pass |
| Definition + use внутри nested list | Definition global, `x` → `/new`, N | Pass |
| Remove used definition в nested list | `Definition in use`, source прежний | Pass |
| Collision с definition вне quote/list | `Definition conflict` | Pass |
| Cascade: list — единственный block QUOTE | Explicit Unsupported, source прежний | Pass |
| Lazy/TAB/open fence nested mapping | Explicit Unsupported | Pass |

### Проверки

- Windows UCRT64 Debug: `cmake --build build-task005 --parallel`,
  `ctest --test-dir build-task005 --output-on-failure` — **3/3 passed**.
  MD4C suite — **225 passed**, 0 failed/skipped.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build с
  `-DMARKPAD_BUILD_MD4C_SPIKE=ON`; CTest — **3/3 passed**.
- Ubuntu sanitized Debug: `-fsanitize=address,undefined`,
  `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`,
  `UBSAN_OPTIONS=halt_on_error=1`; CTest — **3/3 passed**.
- `git diff --check --ignore-space-at-eol` и сборка без warnings пройдены.
  Google clang-format в среде отсутствует; dry-run не выполнялся.
- Native visual QA, clang-tidy и remote CI не запускались.

### Ограничения и следующий шаг

- Поддержаны parents LI/QUOTE и перечисленные nested forms; indent/outdent,
  list/quote parents за пределами LI/QUOTE и произвольные mixed containers —
  [Task 020](task_020.md) и последующие.
- Lazy continuations, structural TAB, open fences и внешние контексты без
  доказуемого virtual line mapping остаются explicit unsupported.
- Standalone HTML blocks и indented code blocks по-прежнему не входят в wrap
  selection; inline HTML внутри P сохраняется.
- Ожидаемый AST строится из проверенного container/subtree и независимых
  count/kind/start/tightness/task assertions; полный ручной transformed AST без
  повторного parse не реализован. Полный parse — принятый компромисс.
- UI nested list/quote команд не добавлен; коммит/push не создавались.
  Task 014 остаётся In Progress до 020 и финальной приёмки.
