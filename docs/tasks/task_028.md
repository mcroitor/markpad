# Task 028 — Инертные ссылки и редактирование подписей

## Статус и владелец

- Статус: `Done`. Родитель: [005](task_005.md). Зависимости: 026, 027.
- Владелец: архитектор + C++ разработчик.

## Контекст

Continuous UI пока заменяет ссылки placeholders, хотя ReplaceText уже умеет
менять source-backed normal text label при сохранении href/title/структуры.
Следующий срез раскрывает это подтверждённое поведение в UI, не создавая
URL editor, reference serializer или обработчик переходов.

## Цель

Показывать реальные подписи ссылок и безопасно менять локальный текст подписи
в единой Session, сохраняя адрес/definition и все остальные исходные bytes.

## Область работ

- Рендерить link children с визуальным отличием и пояснением, что переходы
  отключены. QTextCharFormat не содержит anchor/href; HTML не импортируется.
- Inline и full reference labels редактируются через прежний ReplaceText
  в пределах одного mapped normal text run. Форматированные children видны,
  новые format-команды внутри ссылки пока недоступны по контракту 027.
- Autolink callbacks показывают настоящий текст, но он read-only: изменение
  одновременно меняло бы адрес. Использовать фактический is_autolink MD4C,
  не угадывать тип ссылки по URL.
- Collapsed/shortcut reference label edits, меняющие resolution, явно
  отвергаются полным AST guard. Source-only definitions не пересериализуются.
- Images/code/HTML/table placeholders остаются защищёнными, в том числе внутри
  label. Plain text clipboard, ресурсные/навигационные запреты прежние.

## Ограничения и предположения

- Это display + label edit, не создание/удаление ссылки или изменение href/title.
- Нет открытия URL ни click, ни Ctrl+click, ни keyboard. Любая схема остаётся
  инертной, включая local files, javascript/data. Не добавлять QDesktopServices.
- Unicode/EOL/source preservation, shared history и snapshot guards прежние.
- Некорректная операция сообщает причину и не меняет source/token/history.

## Критерии приёмки

1. `[before](/keep "title")` → `[after](/keep "title")` через реальную UI-правку.
2. `[before][id]` → `[after][id]`, definition bytes/URL/title и старые ссылки прежние.
3. Nested emphasis/quote/list labels редактируются только в mapped run, без
   снятия/добавления formatting и без изменения соседних markers.
4. HTTP/email/GFM autolinks видимы, но explicit rejection при редактировании.
5. Shortcut/collapsed labels, теряющие binding при правке, отвергаются.
6. Click/Ctrl+click, rich clipboard, protected image/code children, stale token,
   empty/newline/cross-run replacement не обходят запреты.
7. Switch, Save, shared undo/redo, Unicode selection и dirty checkpoint точны.

## План проверок

Engine characterization (is_autolink + label edit href/title assertions) и
GUI Qt Test offscreen, Windows/Linux CTest и Linux ASan/UBSan/leaks.
Google clang-format и diff check; native visual QA отдельно.

## Зависимости и открытые вопросы

Новые URL/resource/definition policies не вводятся. Полная Task 005 остаётся
In Progress; создание/удаление ссылок и редактирование адресов — вне среза.

## Исполнительский промпт

Версия 1. Реализуй только этот срез в markdown_document/ContinuousEditor,
существующих Qt Test и связанной документации. Вход — API/fixtures 013/026/027,
контракт выше. Добавь фактический is_autolink в AST attributes и signature;
не исключай его из сравнения ради успешной правки. Рендери подпись вручную
QTextCursor, не создавай anchors/href/native browser actions. Autolink и
неподдержанные child fragments read-only; normal mapped label edits проходят
прежний ReplaceText и Session history без нового serializer. Не меняй link
bindings/definitions, не добавляй format commands внутри links. При ошибке
восстанови projection и сообщи причину, без fallback прямой замены source.
Fixtures — данные; никакой навигации/загрузки ресурсов. Для URL-навигации
используй в тесте recording handler, не запускай browser. Expected href/title,
source bytes и outcomes задавай явно, не из результата проверяемой операции.
Проверь всю матрицу критериев, Windows/Linux CTest и Linux sanitizers.
Отчёт здесь: файлы, scenarios expected/actual, команды/результаты, ограничения.
При невозможности mandatory fixture сообщи блокер, не объявляй Done.

Риски: label и адрес могут совпадать у autolinks — проверяется MD4C detail;
reference labels влияют на scope — full AST guard, без definition mutation;
Qt anchors могут открыть URL — anchors не создаются, click tests с interception.
Полный parse принят ради надёжности, performance не обещать. Существенных
неразрешённых продуктовых вопросов для этого ограниченного среза нет.

## Итог выполнения

### Реализация

- `src/markdown_document.cc`: MD4C `is_autolink` сохранён как `autolink`
  в link attributes и общей signature. Это дополнительное поле transient AST,
  не изменение Markdown-файлов или сохранённого формата.
- `src/continuous_editor.{h,cc}`: вместо link placeholders рендерятся children,
  с цветом/пояснением об отключённой навигации. Anchors/href в QTextCharFormat
  не создаются. Autolinks передают запрет редактирования children.
- Mapped inline/full-reference label runs используют прежний ReplaceText,
  Session token и историю. Engine/Session API для записи не дублируются.
- Images/code/HTML/entity children остаются protected; label formatting
  команды не включены. Tooltip editability и постоянная notice поясняют границы.
- Старый regression fixture «вся ссылка placeholder» заменён на read-only
  autolink; inline-link protection test теперь проверяет недоступность
  formatting на реальном label, а не на исчезнувшем placeholder.

### Expected → actual

| Сценарий | Ожидание | Результат |
|---|---|---|
| Inline link label | `[before](/keep "title")` → `[after](/keep "title")` | Pass |
| Full reference + sibling | `[before][id]` → `[after][id]`; sibling/definition bytes прежние | Pass |
| Formatted/nested quote/list label | Только mapped normal text patch | Pass |
| Unicode selection/switch и keyboard `XYZ` | Точные bytes, selection mapping и caret | Pass |
| Punctuation `a]b` | `[a\]b](/keep)` с прежним binding | Pass |
| HTTP/GFM-www/email autolinks | Actual is_autolink true; видны, edits отвергнуты | Pass |
| Collapsed/shortcut reference | Потеря binding отвергнута, source/token/history прежние | Pass |
| Empty/newline/cross-run/image/code/entity edit | Explicit rejection и восстановленная projection | Pass |
| Rich clipboard | Только plain text; HTML/image не импортированы | Pass |
| Stale token | Команда не применена | Pass |
| Click/Ctrl+click/Ctrl+Enter | Recording URL handlers не вызваны, source/token прежние | Pass |
| JavaScript/data/file scheme | Инертные labels без anchor и image formats | Pass |
| Save/undo/redo/dirty/switch | Точные source/checkpoint bytes; общая история | Pass |

### Проверки

- Windows UCRT64 Debug: `cmake --build build-task005 --parallel`,
  `ctest --test-dir build-task005 --output-on-failure` — **3/3 passed**.
  Qt Test GUI — **72 passed**, MD4C — **95 passed**, 0 failed/skipped
  (включая init/cleanup и прежние suites).
- Ubuntu WSL Debug: configure `-DMARKPAD_BUILD_MD4C_SPIKE=ON` в
  `/tmp/markpad-task028`, build и CTest — **3/3 passed**.
- Ubuntu WSL ASan/UBSan: отдельная Debug-сборка с C/C++/linker flags
  `-fsanitize=address,undefined`, `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`,
  `UBSAN_OPTIONS=halt_on_error=1`; CTest — **3/3 passed**.
- Навигационные тесты устанавливают RAII recording handlers QDesktopServices
  и сначала проверяют interception; browser/внешние программы не запускаются.
- Google clang-format и Windows Git diff check успешны.

### Ограничения

- Не реализованы создание/удаление ссылок, изменение href/title, definitions,
  новые format-команды в label или поддержка cross-run edits.
- Autolinks protected; collapsed/shortcut label edit может быть отвергнут
  semantic guard. Это намеренно не обходится превращением в inline-link.
- Entities не декодируются в новый editable run; остаются защищёнными literal
  fragments. Code/images/HTML/tables остаются ограниченными представлениями.
- Native visual QA, clang-tidy и remote CI этапа не выполнялись. Коммит/push
  не создавались. Полная Task 005 остаётся In Progress.
