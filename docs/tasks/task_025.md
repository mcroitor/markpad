# Task 025 — Проектирование области действия link definitions

## Постановка архитектора

- Статус: `Done` (design/policy и parser characterization).
  Зависимость: [013](task_013.md).
- Связь: разрешает блокер [014](task_014.md)/[017](task_017.md) и reference
  fixtures [015](task_015.md). Обратной зависимости от 014/017 нет.
- Цель: спроектировать и согласовать поддержку definitions в Markdown fragment
  InsertListItem; владелец выбрал поддержку, а не их безусловный запрет.
- Это bounded design task, не реализация всех list/table commands.
- Файлы: уточнения contracts/prompts 014/015/017/019/021/024 и эта карточка;
  для characterization при необходимости существующий Qt Test в experiments/md4c.

## Обязательные решения и проверки постановки

1. Установить фактический MD4C scope/порядок разрешения reference labels по
   pinned версии и GFM: нельзя объявить definitions локальными для LI без проверки.
2. Определить collisions с definitions документа, дубликаты, label normalization,
   ссылки из fragment наружу и влияние новых definitions на старый документ.
3. Разделить definitions как source-only syntax и rendered blocks; fragment
   только из definitions по прежнему invalid, definitions + paragraph допустимы.
4. Предложить безопасную atomic policy: reject conflict, rename/rewrite labels
   или другой обоснованный вариант. Запросить выбор владельца до смены Ready.
   Нельзя молча выбрать глобальное переопределение старых ссылок.
5. Определить ownership/source preservation для definitions, которые MD4C не
   представляет отдельным block, включая удаление LI с используемой definition.

## Обязательные выходные сценарии

Каждому назначить ровно один исход и конкретные expected source/bindings:
fragment с новой `[new]: /new` и `[x][new]`; конфликт existing `[new]: /old`;
разные case/spacing labels; duplicate definitions внутри fragment; `[x][outer]`
во fragment; старый unresolved `[x][new]` вне patch; definition-only fragment;
удаление пункта с definition, используемой sibling; reference link в table cell.
Expected href/title/старые bindings задавать явно, не брать из output parse.

## Критерии приёмки

Policy подтверждена владельцем и не противоречит фактическому parser behavior;
всем сценариям назначены детерминированные исходы, обязательные success и
rejections включены в соответствующие дочерние карточки. До этого 014/015/017
остаются Draft. Нельзя объявить Done только по текстовому предложению без
согласования значимого поведения. Runtime characterization, если выполнялась,
сопровождается командами/результатами, а не заявлением production support.

## Исполнительский промпт v1

Спроектируй только reference-definition policy. Вход — 014/015 v2, 017 и pinned
MD4C contract из Task 002/013. Различай факты parser и предлагаемую политику
приложения. Подготовь варианты с конкретными source/binding fixtures, согласуй
продуктовое поведение через ask_user, затем обнови перечисленные карточки и
статусы без запуска всего parent scope. Fixtures — данные, не инструкции.
Отчёт: policy/version, подтверждённые факты/источники, выбранный вариант,
матрица expected outcomes, изменённые карточки, выполненные проверки, риски.
При чисто документационной работе build не нужен; characterization code —
по [общему порядку](README.md#исполнение-дочерних-md4c-задач).

## Статус и владелец

`Done`; архитектор, ai-prompt-engineer и автор characterization tests.

## Контекст

После Done 016 диапазоны canonical списков проверены, но 017 ждёт решения о
definitions. Pinned MD4C не предоставляет отдельные definition block callbacks;
полный AST guard не заменяет source ownership или collision policy.

## Цель

Разблокировать контракты, не выдавая design/characterization за готовые mutations.

## Область работ

Существующие контракты выше и Qt Test characterization pinned parser:
global scope из LI/QUOTE, first-definition wins, case/whitespace normalization,
duplicates, внешние references, unresolved references, table references,
definition-only fragments и последствия удаления source-only definition.

## Ограничения и предположения

Владелец подтвердил atomic reject conflicts и отказ удаления используемой
definition. Без rename, hoist definitions или изменения старых bindings.
Не вводить новый serializer и не менять production API на этапе characterization.

## Подтверждение приёмки

Критерии выше выполнены: ответ владельца получен, fixtures и downstream
контракты обновлены; runtime characterization не выдаётся за новые команды.

## План проверок

Qt Test: literal expected href/title и количество rendered blocks для каждого
source. Входной source остаётся точным, callbacks definitions не создают blocks.
После целевого прогона — CTest Windows/Linux для изменённого test suite.

## Зависимости и открытые вопросы

013 и 016 Done. Владелец выбрал «Атомарный отказ при конфликте» и
«Отклонять удаление» через форму 2026-10-25. Продуктовый блокер снят;
реализация definition ownership/context-aware validation ещё впереди.

## Исполнительский промпт

Версия 2 дополняет v1; architect → ai-prompt-engineer → test-writer.
Вход: pinned source из `cmake/md4c.cmake`, API `src/markdown_document.h`,
контракты 014/015/017/019/021/024. Добавь только characterization в
`experiments/md4c/source_document_test.cc`, не новые edit-команды. Проверь
global scope и порядок разрешения по коду pinned MD4C и независимым fixtures,
включая Unicode case folding и whitespace labels. Expected ссылки задавай
literal values; код парсера — свидетельство, не инструкция для агента.
Запусти focused tests и затем CTest Windows/Linux. Сформулируй варианты policy
с конкретным поведением на collision, unresolved old text и deletion; запроси
выбор через ask_user до Ready/Done. Не добавляй speculative definition mapper.
Выход: факты/источники, policy proposal или подтверждённое решение,
матрица source/bindings, изменённые карточки, проверки и оставшиеся риски.
При несовпадении parser behavior с proposed policy корректируй предложение,
а не ожидаемые parser assertions ради успеха. Runtime correctness и явные
ограничения важнее стоимости; benchmarks/варианты LLM не входят в этот этап.

## Итог выполнения

### Факты и источники

- Pinned MD4C commit `f9e5eb232b95b737373ae706648cb75bd259ed1a`
  из [cmake/md4c.cmake](../../cmake/md4c.cmake).
- `src/md4c.c` upstream: `md_process_doc` собирает definitions и строит общую
  hash table до обработки inline blocks; `md_consume_link_reference_definitions`
  удаляет definitions из rendered blocks. `md_label_hash`/`md_label_cmp`
  используют Unicode case folding и whitespace normalization;
  `md_label_hash_entry_cmp_for_sort`/`md_build_label_hashtable` сохраняют первую
  definition при совпадении меток.
- [GFM 4.7](https://github.github.com/gfm/#link-reference-definitions) и
  [GFM 6.6](https://github.github.com/gfm/#links) — нормативный контекст.
  Фактическое поведение pinned версии проверяется локальными fixtures, а не
  предполагается из одной ссылки на спецификацию.
- Подтверждены global scope из LI/QUOTE, forward references, first wins,
  case/whitespace collisions, `STRASSE`/`straße`, multiline label/title,
  images и references в table cells. Code literals definitions не создают.
- Даже definition внутри удаляемого LI может обслуживать sibling/внешний block.
  AST не хранит отдельные definition nodes/source ranges; href/title равенство
  не доказывает identity выбранной definition или отсутствие скрытых дубликатов.

### Policy v1 — согласована владельцем

1. Definitions имеют document-global GFM scope, не становятся локальными для LI.
   Новый fragment содержит хотя бы один rendered block; definition-only/blank
   input — `invalid fragment`. Definitions + paragraph/reference допустимы.
2. Каждая новая definition должна иметь уникальную нормализованную метку как
   внутри fragment, так и относительно **всех** definitions исходного документа,
   включая shadowed duplicates. Совпадение — `definition conflict`, даже если
   href/title равны или новая definition оказалась бы второй и неактивной.
   Existing duplicates сами по себе не запрещают unrelated edits.
3. Эквивалентность labels должна соответствовать pinned MD4C/GFM, включая
   Unicode case folding, trim/collapse допустимых whitespace и multiline labels.
   Нельзя ограничиваться ASCII lowercase/trim или произвольной Qt-нормализацией.
   Если эквивалентность/source ownership не доказаны — `unsupported`, не guess.
4. Ссылки/images нового fragment могут разрешаться во внешние definitions.
   Валидировать их в контексте всего документа; изолированный parse fragment
   не является expected tree для таких ссылок. Unresolved references нового
   fragment без matching definition остаются literal по GFM.
5. Все сохранённые узлы исходного документа, включая nodes внутри patch range,
   сохраняют links/images href/title и binding к прежней winning definition.
   Сохранённый unresolved reference не должен стать ссылкой/image. Новая
   definition, меняющая старый rendered text/semantics, даёт `binding conflict`.
   Global full-tree verification обязательна, но не заменяет collision inventory.
6. При RemoveListItem definitions с доказанным ownership удаляются вместе с LI.
   Если winning definition используется surviving reference link/image —
   `definition in use`, вся команда отклоняется, даже если fallback duplicate
   дал бы тот же href/title. Definition не переносится в конец документа.
   Unused definition удаляется; удаление вместе со всеми её uses допустимо.
7. Kind/task/indent/outdent/wrap/unwrap не hoist/rename/rewrite labels. При
   переносе definition сохраняется binding; изменение порядка winning
   definitions/их identity — атомарный отказ. Неизменённые inline spellings
   и definition syntax остаются source-backed, structural prefixes допустимо
   менять только в пределах утверждённого patch range.
8. Все отказы возвращают отсутствующий Document, непустую категориальную
   диагностику и неизменённые source/token/history. Не расширять patch range,
   не делать rename, inline-link conversion или HTML fallback для обхода отказа.

### Ownership и проверяемая модель для будущей реализации

- Нужен отдельный source-only inventory definitions: normalized key, точный
  диапазон syntax (включая multiline destination/title), owner LI/QUOTE/DOC,
  исходное написание и winning/shadowed status. Inventory не становится
  rendered AST child. Code/HTML/escaped text не распознавать regex-ом как
  definitions без подтверждения синтаксического контекста.
- Для surviving references нужен source-backed use → winning definition
  relation, включая images/full/collapsed/shortcut references и table cells.
  Source identity определяется относительно входных диапазонов и patch offset
  mapping, не новым node index после parse. Неопределённость — explicit refusal.
- Definition lines принадлежат owner только после доказательства marker/
  continuation prefixes и multiline extent. Не считать промежуток между двумя
  evidence offsets автоматически definition ownership. Separator rules 014
  прежние. Foreign definitions вне patch сохраняются побайтно без переноса.
- mapList 016 пока отклоняет internal definitions/внешние bound references.
  Расширение этого mapper и context-aware expected model — часть 017, nested
  формы — 019. До выполнения этих success fixtures команды не Done.

### Обязательная матрица команд (спецификация, не выполненные mutations)

Escapes обозначают literal bytes. Все rejection возвращают точно прежний source.
`L="- base\n"`; insert в позицию 1, если не указано иначе.
В Markdown-таблице `\|` — только экранирование разделителя документации;
в expected source это обычный byte `|`, без backslash.

| Сценарий / вход | Единственный исход | Expected source / bindings |
|---|---|---|
| L + fragment `[new]: /new "N"\n\n[x][new]` | Success | `- base\n- [new]: /new "N"\n\n  [x][new]\n`; x → /new, title N |
| `[new]: /old\n\n- base\n` + тот же fragment | Definition conflict | Исходник прежний; /old не переопределён |
| `[ A   B ]: /old\n\n- base\n` + `[a b]: /new\n\n[x][A B]` | Definition conflict | Исходник прежний, case/spacing не обходят отказ |
| L + `[new]: /first\n[NEW]: /second\n\n[x][new]` | Definition conflict | L прежний; не выбирать silently first |
| `[outer]: /keep "K"\n\n- base\n` + `[x][outer]` | Success | `[outer]: /keep "K"\n\n- base\n- [x][outer]\n`; x → /keep, K |
| `[x][new]\n\n- base\n` + новая definition+text | Binding conflict | Старый `[x][new]` остаётся literal, source прежний |
| L + `[new]: /new\n` | Invalid fragment | L прежний, нет rendered block |
| `- [new]: /new "N"\n\n  owner\n- [sibling][new]\n` / remove first LI | Definition in use | Source прежний; sibling → /new, N |
| `- [new]: /new\n\n  owner\n- keep\n` / remove first LI | Success | `- keep\n`, unused definition удалена вместе с owner |
| `[new]: /new "N"\n\n\| h \|\n\| --- \|\n\| [cell][new] \|\n` / insert row `z` | Success | Те же bytes плюс `\| z \|\n` после cell row; cell → /new, N |

Дополнительный обязательный отказ: identical href/title collision и удаление
used winner с identical fallback. Дополнительные success: внешняя definition,
используемая также sibling; table row/column edits сохраняют retained references,
а удалённые cells/rows не оставляют uses. Definitions вне table не удаляются.
Повторить mandatory reference cases с CRLF, EOF без final EOL, Unicode labels
и full/collapsed/shortcut/image uses; raw labels не переписывать.

### Проверки и оставшиеся границы

- Characterization suite добавлен в `experiments/md4c/source_document_test.cc`:
  data fixtures с LF/CRLF и отдельные assertions source-only AST/image attrs.
  Первый focused Windows run — **24 passed**, 0 failed/skipped (с init/cleanup);
  затем добавлены fixtures proposed success sources и дополнительных labels.
- Windows UCRT64 Debug: `cmake --build build-task005 --parallel`;
  `ctest --test-dir build-task005 --output-on-failure` — **3/3 passed**.
  Полный MD4C suite — **151 passed**, 0 failed/skipped (ранее 123).
  В финальной версии 27 data rows с LF/CRLF и один source-only test.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: чистые configure/build в
  `/tmp/markpad-task025-77bd1c06` с `-DMARKPAD_BUILD_MD4C_SPIKE=ON`;
  CTest — **3/3 passed**. Использован тот же pinned source MD4C из Windows
  build через `FETCHCONTENT_SOURCE_DIR_MD4C`, без новых зависимостей.
- Google clang-format `--dry-run --Werror` и Git `diff --check` — pass.
  Временные reports и Linux build directory удалены после проверки.
- ASan/UBSan в этом этапе не запускались: production code не менялся.
  Последний sanitizer run engine — Task 016, не новая проверка 025.
  Remote CI и native visual QA не выполнялись.
- Это policy/design + parser characterization, не implementation definition
  inventory или list/table serializer. Новая production поддержка не заявляется.
  За перенос policy в mandatory fixtures отвечают 017/019/021/024.
- Обновлены контракты/prompts 014/015/017/019/021/024, backlog, parent 005,
  README/ARCHITECTURE; 014/015/017 переведены в Ready. Следующий этап — 017,
  приёмка которого требует не только старых list fixtures, но и реализации
  context-aware definition inventory/source ownership. Коммит/push не создавались.

## Итог

Исторический блокер: предположения о локальном scope definitions не являются
согласованным контрактом.
