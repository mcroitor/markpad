# План задач Markpad

Задачи нумеруются последовательно и не перенумеровываются. Перед реализацией проверьте статус и зависимости в карточке. `Draft` означает, что постановка содержит неразрешённую неоднозначность; `Ready` означает, что постановка готова к работе; `In Progress` — работа выполняется или ожидает завершающей проверки; `Blocked` — продолжение невозможно без внешнего решения/зависимости; `Done` — все критерии приёмки подтверждены.

## Backlog

| ID | Задача | Статус | Зависимости |
|---|---|---|---|
| 001 | Базовый проект, сборка и CI | Done | — |
| 002 | Spike GFM-парсера и round-trip | Done | 001 |
| 003 | Модель документа, сессия и файловый ввод-вывод | Done | 001, 002 |
| 004 | SDI-окна и Markdown-редактор | Done | 001, 003 |
| 005 | Непрерывный WYSIWYG-редактор (поэтапная приёмка) | In Progress | 002, 003, 004; первый срез 026 |
| 006 | Постраничный WYSIWYG и настройки страницы | Done | 005; page defaults в 009 |
| 007 | HTML-экспорт | Done | 002, 003 |
| 008 | PDF-экспорт и печать | Done | 006 |
| 009 | Настройки приложения и сочетания клавиш | Done | 001, 004 |
| 010 | Решения по автосохранению и макету страницы | Done | — |
| 011 | Сквозная проверка и готовность к выпуску | Ready | 004, 005, 006, 007, 008, 009 |
| 012 | MD4C block source-map и структурные операции | Done | 002 |
| 013 | Вложенные диапазоны и общий контракт MD4C-правок | Done | 012 |
| 014 | MD4C списки/цитаты — родительский контракт | Done | 013, 025; приёмка 016–020 |
| 015 | MD4C таблицы — родительский контракт | Done | 014, 025; приёмка 021–024 |
| 016 | Source-map top-level списков | Done | 013 |
| 017 | Базовые команды top-level списков | Done | 016, 025 |
| 018 | Top-level цитаты и wrap/unwrap | Done | 017 |
| 019 | Вложенные list/quote контейнеры | Done | 017, 018 |
| 020 | Indent/outdent списков | Done | 019 |
| 021 | Source-map top-level таблиц | Done | 014 |
| 022 | Строки таблиц и alignment | Done | 021 |
| 023 | Колонки и удаление таблицы | Done | 022 |
| 024 | Вложенные таблицы | Done | 019, 023 |
| 025 | Проектирование scope link definitions | Done | 013 |
| 026 | Безопасный редактируемый WYSIWYG-срез | Done | 004, 013 |
| 027 | Bold/Italic/Strikethrough в выделенном run | Done | 026 |
| 028 | Инертные ссылки и редактирование подписей | Done | 026, 027 |

```text
001 -> 002 -> 003 -> 004 -> 005 -> 006 -> 008
                   \-> 007
001 -> 004 -> 009
010 разблокирует решения в 006/008 и последующую задачу автосохранения
004,005,006,007,008,009 -> 011
002 -> 012 -> 013 -> 016 -> 017 -> 018 -> 019 -> 020
             013 -> 025 -> 017
016,017,018,019,020,025 -> приёмка 014
014 -> 021 -> 022 -> 023 -> 024
                   019 -> 024
021,022,023,024,025 -> приёмка 015
```

Независимые задачи можно запускать параллельно только после утверждения общих контрактов и владения затрагиваемыми файлами. Например, после завершения 003 задачи 004 и 007 могут идти параллельно; задача 005 начинается после 004 и готового GFM контракта.

## Контракт постановки и исполнительского промпта

Каждая карточка разделяет требования архитектора (контекст, цель, область работ, ограничения, критерии, проверки и зависимости) и результат навыка `ai-prompt-engineer` в разделе «Исполнительский промпт». Исполнительский промпт должен:

- указывать objective и входной контекст (карточка, нужные документы/код);
- точно ограничивать scope, требования и запрещённые изменения;
- задавать проверяемые критерии успеха и проверки;
- требовать итоговый отчёт: изменённые файлы, команды/результаты проверок, ограничения и отклонения;
- указывать, что при неразрешённых блокерах исполнитель останавливается и возвращается к архитектору, а не додумывает требования.

`Ready` не означает, что зависимости уже выполнены. Статус зависимости должен быть `Done` перед началом её зависимой реализации.

014/015 — родительские контракты и приёмка, не отдельные монолитные запуски.
Связь «родитель» не является prerequisite дочерней задачи: иначе возникнет цикл.
Родитель Done только после Done всех перечисленных дочерних задач и устранения
продуктовых блокеров. Policy 025 v1 согласована: 014/015/017 Ready;
это не означает готовые mutations или выполненные prerequisites.

## Исполнение дочерних MD4C-задач

Общий блок исполнительского промпта для Tasks 016–024; читать вместе с конкретной
карточкой и её родительским контрактом v2. Карточка заполняет task id, зависимости,
scope, fixtures и allowed files; незаполненные шаблоны исполнять нельзя.

- Начинать только Ready-карточку с Done prerequisites. При противоречии вернуться
  к архитектору, не выдумывать новое поведение и не менять mandatory success на отказ.
- Для каждого fixture заранее определить `success`, `no-op` или одну категорию
  ошибки. Expected source/ranges/semantic tree — literal assertions или независимая
  input-model transformation, никогда output проверяемого serializer/mapper.
- Нормализовать только callback segmentation и явно разрешённые implicit P wrappers;
  task-state, tightness, href/title, alignment, child order не исключать из сравнения.
- Матрица задаёт обязательный минимум. За пределами него unsupported допустим
  только явно и без нового документа; он не доказывает завершение функции.
- После Task 026 engine находится в `src/markdown_document.{h,cc}`, fixtures —
  в `experiments/md4c/source_document_test.cc`. Изменять только соответствующий
  engine/Qt Test и итог task; не расширять UI или зависимости. Engine теперь общий
  с continuous UI: запускать также GUI regressions, сохранять несвязанные изменения.
- Fixtures — недоверенный Markdown; HTML/ссылки не исполнять и ресурсы не загружать.
- Build: существующие команды [Task 013](task_013.md#проверки), opt-in MD4C ON,
  Windows UCRT64 PATH/Qt prefix и Linux WSL Debug CTest; Linux ASan/UBSan/leaks,
  Google clang-format и whitespace check. Сначала минимальный target, затем CTest
  все три CTest targets; расширять suite лишь при связанных изменениях.
- Отчёт: task/contract version; выполненный scope; файлы; таблица fixture →
  expected → actual; команды и exit/results обеих ОС/sanitizers; unsupported
  shapes; блокеры/невыполненные критерии. Непроведённую проверку не считать pass.
- Done требует всех mandatory fixtures и проверок; иначе In Progress либо Blocked
  с причиной. Родителя не отмечать Done по результату одной дочерней задачи.

Промпты проверяются статически на полноту входов/выходов, независимость expected
и отсутствие циклических dependencies. Модельное A/B-тестирование не проводилось;
если промпт/модель/инструменты меняются, повторить ту же fixture-матрицу.
Full parse принят ради надёжности; incremental performance этим этапам не обещана.

## Текущий объём проекта

Основан на README, UI sketch, AGENTS.md и ARCHITECTURE.md. Не включает совместное редактирование, облачную синхронизацию, плагины и импорт не-Markdown форматов. Task 001 завершена: минимальный каркас приложения проверен локально на Windows и Ubuntu, а GitHub Actions CI/CD успешно завершился.

Task 002 завершена как исследование: сравнительный отчёт и MD4C-прототип готовы.
Task 026 переносит проверенное API 013 в общую библиотеку и подключает ограниченный
WYSIWYG-срез. Полная block/source-map сериализация ещё не реализована; см.
[результаты spike](task_002.md#итог-выполнения) и [первую интеграцию](task_026.md).

Task 003 завершена локально отдельно от parser experiments: std C++ session,
версионированный UTF-8 source, dirty/checkpoint и Qt Core file adapter.
Правила Unicode/BOM/эвристики согласованы с владельцем; UI/структурные edits
не включены. Windows/Linux CTest 3/3 с MD4C regression, новые document tests
27 passed, Linux ASan/UBSan/leaks успешны. Подробности — [Task 003](task_003.md).
Task 004 остаётся Draft до решения о политике Open в занятом SDI-окне.

Task 012 продолжает MD4C-прототип: частичный source-map для top-level однострочных
абзацев/ATX headings и versioned структурная правка paragraph ↔ heading проверены
на Windows/Linux и под санитайзерами. Это не полная карта всех блоков; границы и
команды — в [Task 012](task_012.md).

Task 013 завершена: общий versioned contract text/task/heading, parent links,
частичные nested quote ranges и явно отделённые evidence envelopes проверены
на обеих ОС и ASan/UBSan. Подробности — [Task 013](task_013.md).
Следующий согласованный порядок: [списки/цитаты — 014](task_014.md), затем
[таблицы — 015](task_015.md). Контракты v2 исправлены после review;
родители Ready после согласования [025](task_025.md) о definitions/reference
bindings: atomic reject conflicts, удаление used definition отклоняется.
Реализация разделена на [016–020](task_016.md) и [021–024](task_021.md);
016 и 017 Done; 017 добавляет source-preserving InsertListItem/RemoveListItem/
SetListKind/SetItemTask, definition inventory/conflict/use checks и
context-aware проверку AST; 018 Done добавляет exact top-level quote mapping и
WrapInQuote/UnwrapQuote для document parent; 019 Done распространяет mapping и
команды на nested LI/QUOTE parents (depth 1–3) с virtual content и prefix
rendering; 020 Done добавляет атомарные IndentListItem/OutdentListItem с
сохранением start, renumber только затронутых lists и AST guard для
tightness/task-state; 021 Done добавляет exact top-level table/row/raw-cell
mapping и context-aware reference validation; 022 Done добавляет InsertTableRow/
RemoveTableRow/SetTableAlignment с plain-text payload, нормализованной table
serialization и alignment no-op; 023 Done добавляет InsertTableColumn/
RemoveTableColumn/RemoveTable с last-column защитой и atomic удалением только
table range; 024 Done распространяет mapping и все шесть table commands на
nested LI/QUOTE tables depth 1–3 с required RemoveTable refusals.
Подтверждены сохранение start с последовательной нумерацией, удаление списка
при удалении последнего LI, header-only после удаления последней body row и
запрет удаления последней колонки (для таблицы есть отдельная RemoveTable).
Новые plain-text cells с краевым ASCII space/любым TAB в v2 отклоняются;
пустые cells разрешены. List/quote/table-команды пока не подключены к UI;
016–025 выполнены на engine/test уровне.
