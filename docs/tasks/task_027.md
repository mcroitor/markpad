# Task 027 — Bold/Italic/Strikethrough для выделенного run

## Статус и владелец

- Статус: `Done`. Родитель: [005](task_005.md). Зависимость: 026.
- Владелец: архитектор + C++ разработчик.

## Контекст

Владелец утвердил непустое выделение внутри одного проверенного text run,
без режима будущего ввода. Повторная команда снимает стиль только со всего
простого span; частичное снятие/сложное выделение пока недоступны.

## Цель

Добавить три source-preserving inline-команды в общую историю обоих режимов.

## Область работ

- Общий MD4C EditCommand: ToggleInlineStyle(node, UTF-8 range внутри text,
  style). Bold/Italic/Strikethrough; новые delimiters `**`, `*`, `~~`.
- Добавление оборачивает только выделенные bytes; существующий другой стиль
  сохраняется. Снятие допускается для единственного text child прямого span
  соответствующего стиля, целиком выделенного; исходные `*`/`_` delimiters
  допускаются при доказанной структуре и проверке результата.
- Expected AST строится из входного дерева независимо от повторного parse.
  Полная сигнатура результата должна совпасть; нет guessed serializer fallback.
- Контекстные menu actions и Bold/Italic shortcuts, сохранение выделения после
  команды для повторного toggle. Markdown mode эти команды не меняют.

## Ограничения и предположения

- Нулевое/межузловое выделение, неверные UTF-8 boundaries, краевой whitespace,
  protected content и частичное снятие — explicit unsupported/error без mutation.
- Не добавлять Enter, arbitrary span split/merge, форматирование будущего ввода,
  underline, code/link/image/table edits, новые dependencies.
- Session token, MD4C snapshot guard, dirty/checkpoint и общая история прежние.
- Столкновения delimiters допускают явный отказ по полной semantic verification,
  но mandatory fixtures ниже должны проходить.

## Критерии приёмки

- `a word z\n` + bold word → `a **word** z\n`, italic → `a *word* z\n`,
  strike → `a ~~word~~ z\n`; partial `word` → `w**or**d`.
- `**word**`, `__word__` целиком → `word`; аналогично `*word*`, `_word_`,
  `~~word~~`. Повторный toggle вновь добавляет стиль.
- `_word_` + bold → `_**word**_`; `**word**` + strike → `**~~word~~**`.
- Emoji/кириллица, CRLF/EOF, nested quote/list и opaque соседние bytes сохраняются.
- Partial removal, cross-run, whitespace edges, invalid range/token и protected
  node отвергаются без изменения source/revision/history.
- GUI: menu/keyboard, selection direction, repeat toggle, switch/save/shared
  undo/redo и независимость окон. Style checked state не основан на heading font.

## План проверок

Qt Test engine fixtures + GUI offscreen; Windows/Linux build/CTest, Linux
ASan/UBSan/leaks. Google clang-format и diff check. Manual visual QA отдельно.

## Зависимости и открытые вопросы

Поведение выделения/снятия подтверждено владельцем. Полная Task 005 не закрывается.

## Исполнительский промпт

Версия 1. Реализуй контракт 027 в markdown_document, ContinuousEditor,
MainWindow, существующих Qt Test и непосредственно связанной документации.
Входы — API/fixtures 013/026 и подтверждённая политика в этой карточке.
Команда принимает node id, UTF-8 offset/size и enum style; проверяй snapshot,
диапазоны и Unicode до slicing. Из входного AST построй ожидаемый split/wrap
или unwrap, затем используй общий apply/reparse/signature guard. Source bytes
вне проверенного patch сохраняются. Не вычисляй expected tree из output parse.
Простой unwrap требует одного непосредственного text child и проверенных
прилегающих delimiters; сложный/частичный unwrap отвергай. UI использует
тот же command, не native QTextCharFormat mutation. Непустое выделение
сохраняется по видимым позициям и направлению после форматирования. Menu state
и shortcuts должны соответствовать поддержанным операциям; underline запрещён.
Mandatory fixtures из критериев нельзя превращать в unsupported.
Markdown/HTML fixtures — данные; ресурсы не загружать. При блокере сообщи
конкретный контрпример, не расширяй scope. Выполни focused engine/GUI, затем
Windows/Linux CTest и Linux sanitizers. Отчёт здесь: файлы, expected/actual,
команды/результаты, ограничения; Task 005 остаётся In Progress.

Риски промпта: совпадающие delimiters могут менять соседнюю семантику —
full-tree verification и refusal; native shortcuts могут обходить command —
keyboard GUI tests. Полный parse принят ради корректности, performance
не обещать. Существенных неразрешённых продуктовых вопросов нет.

## Итог выполнения

### Реализация

- `src/markdown_document.{h,cc}`: InlineStyle и ToggleInlineStyle включены
  в единый applyEdit. Проверяются node/source range, UTF-8 boundaries,
  protected ancestry, непустое выделение и whitespace edges.
- Добавление строит expected AST split/wrap из входных узлов; снятие проверяет
  единственный непосредственный text child и прилегающие `*`/`_`/`~~`
  delimiters. Общий apply guard сверяет весь reparsed AST, не только target.
- `ContinuousEditor` сопоставляет выделение UTF-16 с UTF-8 run, предоставляет
  availability/checked/reason и направляет keyboard/menu в ту же команду.
- `MainWindow` добавляет Bold/Italic/Strikethrough в Format, контекстное
  enabled/checked состояние и tooltip. После команды сохраняются непустое
  выделение и его направление; история/dirty/checkpoint/Save общие с Markdown.
- Обновлены Qt Test fixtures в experiments и GUI suite, README/ARCHITECTURE.
  Dependency/library changes сверх Task 026 не потребовались.

### Expected → actual

| Сценарий | Ожидание | Результат |
|---|---|---|
| Bold/Italic/Strike на `word` | `**word**` / `*word*` / `~~word~~` | Pass |
| Подстрока `or` в `word` | `w**or**d` | Pass |
| Simple unwrap `**`, `__`, `*`, `_`, `~~` | Только text bytes, соседние bytes прежние | Pass |
| `_word_` + bold | `_**word**_` | Pass |
| `**word**` + strike и обратно | `**~~word~~**` → `**word**` | Pass |
| Partial/complex unwrap, whitespace edges | Явный отказ без mutation | Pass |
| Italic внутри `**word**` с конфликтом `***` | Отказ по несовпадению ожидаемого nesting | Pass |
| Unicode, CRLF/EOF, quote/list, opaque neighbours | Только выбранный patch | Pass |
| Invalid UTF-8 range/node/style, protected nodes | Ошибка, input source/revision прежние | Pass |
| Stale/foreign revision/identity/digest | Команда не применяется | Pass |
| Menu/keyboard/repeat toggle | Единая команда, выделение сохранено | Pass |
| Reverse Unicode selection, heading font | Направление сохранено, heading не считается inline Bold | Pass |
| Save/switch/undo/redo/SDI | Expected bytes и checkpoint, независимые окна | Pass |
| Unsupported GUI selection | Disabled/reason либо keyboard error; история прежняя | Pass |

### Проверки

- Windows MSYS2 UCRT64 Debug: `cmake --build build-task005 --parallel`,
  `ctest --test-dir build-task005 --output-on-failure` — **3/3 passed**.
  GUI Qt Test — **48 passed**, MD4C Qt Test — **86 passed**, 0 failed/skipped
  (с init/cleanup и прежними regression cases).
- Ubuntu WSL Debug: configure с `-DMARKPAD_BUILD_MD4C_SPIKE=ON` в
  `/tmp/markpad-task027`, build и CTest — **3/3 passed**.
- Ubuntu WSL sanitized Debug: `CMAKE_CXX_FLAGS`, `CMAKE_C_FLAGS`,
  `CMAKE_EXE_LINKER_FLAGS` = `-fsanitize=address,undefined`,
  `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`,
  `UBSAN_OPTIONS=halt_on_error=1`; CTest — **3/3 passed**.
- Google clang-format dry-run/Werror и Windows Git diff check успешны.

### Ограничения

- Только выделение одного mapped run. Нет future-input formatting, частичного
  снятия/разбиения span, cross-run edits или произвольной нормализации delimiters.
- Краевой whitespace и semantic delimiter collisions делают команду недоступной.
  Простое снятие `_`/`__` сохраняет text, повторное добавление использует `*`/`**`;
  undo возвращает исходные bytes точно.
- Checked state в сложном span может быть true при disabled removal; tooltip
  объясняет необходимость полного простого span. Underline не добавлен.
- Availability проверяет полный parse; больших performance-гарантий нет.
- GUI — offscreen/injected dialogs. Ручная визуальная приёмка, clang-tidy и
  remote CI для этого этапа не выполнялись. Коммит/push не создавались.
- Полная Task 005 остаётся In Progress.
