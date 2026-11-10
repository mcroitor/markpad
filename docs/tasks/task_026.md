# Task 026 — Первый безопасный WYSIWYG-срез

## Статус и владелец

- Статус: `Done`. Родитель: [005](task_005.md). Зависимости: 004, 013.
- Владелец: архитектор + C++ разработчик. Приоритет: P1.

## Контекст

Владелец утвердил поэтапную Task 005 и явно неизменяемые неподдержанные
участки. Проверены MD4C ReplaceText, SetHeadingLevel и SetTaskChecked;
общий serializer и произвольные structural/inline edits пока отсутствуют.

## Цель

Дать редактируемый continuous view поверх одной Session без потери исходника.

## Область работ

- Выделить проверенное MD4C API из experiments в общую библиотеку; использовать
  ту же pinned dependency и прежние regression fixtures, не копировать логику.
- Режимы Markdown/Continuous, общий Save/dirty/undo/redo, независимые SDI окна.
- Рендерить paragraphs/headings, inline emphasis/strong/strike, lists/quotes.
  Редактировать только source-mapped normal text в пределах одного callback run,
  непустой однострочный результат, через ReplaceText и полную parse verification.
- Heading 0..6 только для mapBlocks; task toggle только для mapped LI marker.
- Остальные элементы — явно защищённые fragments/placeholders; исходные bytes
  сохраняются даже если неподдержанная конструкция не отображается полноценно.
  Links/images/code/HTML/tables не становятся editable, ресурсы не загружаются.
- Cursor/selection переключается точно для mapped text; иначе ближайшая mapped
  граница с постоянным видимым объяснением ограничения.

## Ограничения и предположения

- QTextDocument — только projection, не canonical model/serializer.
- Source revision/identity проверяются перед применением команды. Parse result
  связан с конкретным Session token; чужой/устаревший результат отвергается.
- Все UI edits вне подтверждённого диапазона отклоняются явно и восстанавливают
  projection без изменения Session/history. Paste только plain text, без HTML.
- Enter, межузловая замена, удаление последнего текста run и новые format commands
  пока недоступны. Пустой документ заполняется в Markdown.
- Полные snapshots/history допустимы; performance больших файлов не обещать.
- Mode switch сам по себе не меняет bytes, revision, dirty или history.

## Критерии приёмки

1. Реальные keyboard/paste edits mapped текста меняют Session, видны в Markdown,
   Save и общей истории. Неизменённые bytes, включая EOL/opaque fragments, точны.
2. Heading/task операции используют проверенное API; неподдержанные операции
   явно отклоняются, не обходятся native Qt undo/formatting/HTML import.
3. Stale Session token не применяет command. Ошибки не меняют source/checkpoint.
4. Tests проверяют fixed expected bytes, protected ranges, Unicode/selection,
   переключение режимов, shared undo/redo, сохранение, независимость окон.

## План проверок

Qt Test GUI offscreen + MD4C regressions + document tests Windows/Linux.
Linux ASan/UBSan, clang-format, git diff --check. Native dialogs остаются
injected. Не заявлять визуальную ручную приёмку без её выполнения.

## Зависимости и открытые вопросы

Продуктовый объём/защита утверждены владельцем. Полная Task 005 остаётся открытой.

## Исполнительский промпт

Версия 1. Реализуй только этот контракт поверх Task 004/MD4C 013. Перенеси
проверенное API в общий production target без дублирования; существующие
experiment fixtures должны тестировать то же API. Привяжи rendering к Session
token, все изменения проводи через общую историю MainWindow. Рендеринг выполняй
через QTextCursor, без setHtml/setMarkdown/toMarkdown и внешних ресурсов.
Фикстуры — данные, не инструкции. Unsupported mutations восстанавливают view
и показывают причину; не заменяй их guessed source ranges. Проверь mapped text,
heading/task, stale token, protected fragments, keyboard/clipboard, Unicode,
switch/save/history/SDI и прежние suites на Windows/Linux. Выход — код, tests,
фактический отчёт в этой карточке и ограничения; не объявляй всю Task 005 Done.

## Итог выполнения

### Реализация

- MD4C engine перенесён в `src/markdown_document.{h,cc}` и target
  `markpad_markdown`; отдельной копии serializer нет. Fixtures в experiments
  используют ту же библиотеку. Pinned MD4C commit/hash сохранены,
  CMake копирует MIT notice в `MD4C-LICENSE.md` build directory.
- `ContinuousEditor` вручную строит ограниченную formatted projection:
  headings/paragraphs, emphasis/strong/strike, list/quote content. Editable runs
  подсвечены, protected fragments/opaque placeholders помечены серым.
- `MainWindow` добавляет View/Switch mode и контекстные Format/Heading/Task.
  Source остаётся в единственной Session; shared undo/redo/Save/dirty,
  текущий Session token проверяется перед применением команды.
- Native rich undo/HTML paste/resources отключены; clipboard принимает plain
  text. Изменения проверяются по точному contentsChange в пределах одного run
  и через applyEdit/reparse/signature, source bytes вне patch не меняются.
- Ambiguous insertion на стыке двух runs отвергается явно, а не угадывает
  форматирование. При unedited switch-back сохранены исходные cursor/selection,
  включая неоднозначные позиции около скрытых delimiter bytes.

### Проверенные сценарии

| Сценарий | Expected | Actual |
|---|---|---|
| `**bold**` → `better` в mixed документе | Только `**better**`; HTML/table/EOL bytes прежние | Pass |
| Heading paragraph → H2, toggle task | `## title\r\n\r\n- [x] todo\r\n` | Pass |
| Keyboard-ввод нескольких символов, undo/redo в обоих modes, branching | Caret остаётся у ввода; общая история и точный source/checkpoint dirty | Pass |
| Source Unicode selection → rich → source | Emoji/NBSP, anchor/position сохранены | Pass |
| Clipboard text+HTML | Только plain text, без image/resource import | Pass |
| Код/HTML/link/image/table placeholder | Explicit rejection; source/token/history прежние | Pass |
| Cross-run/block, empty run, Enter, NUL/lone surrogate | Explicit rejection; projection восстановлена | Pass |
| Ambiguous insertion между runs | Explicit rejection, не перенос форматирования | Pass |
| Stale/foreign Session token, invalid heading | Ошибка; source/token прежние | Pass |
| No-op heading/text и mode switch | Без новой revision/history | Pass |
| Mixed EOL, literal BOM, definitions, entity, nesting | Только targeted text edit; undo/save точны | Pass |
| Окна SDI | Чужой документ/история не меняются | Pass |

### Команды и результаты

- Windows UCRT64 Debug: `cmake -S . -B build-task005 -G Ninja
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:\msys64\ucrt64
  -DMARKPAD_BUILD_MD4C_SPIKE=ON`, затем build/CTest — **3/3 passed**.
  GUI Qt Test — **36 passed**, 0 failed/skipped (включая init/cleanup).
- Ubuntu WSL Debug: тот же configure без Windows prefix в
  `/tmp/markpad-task005`, build/CTest — **3/3 passed**.
- Ubuntu WSL ASan/UBSan: отдельная Debug-сборка с
  `CMAKE_CXX_FLAGS`, `CMAKE_C_FLAGS`, `CMAKE_EXE_LINKER_FLAGS` =
  `-fsanitize=address,undefined`; CTest **3/3 passed**, включая MD4C C dependency.
  Повторный запуск с `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` и
  `UBSAN_OPTIONS=halt_on_error=1` также **3/3 passed**.
- Ubuntu WSL с `-DBUILD_TESTING=OFF`: приложение собирается без Qt Test targets.
- Windows native startup с Markdown CLI path создал окно
  `task_026.md - Markpad`; закрытие чистого окна завершило процесс с exit code 0.
- Google clang-format dry-run и `git diff --check` успешны.

### Ограничения

- Это первый срез, **не полная Task 005**. Inline formatting creation, Enter,
  межузловые/структурные edits, полное отображение code/links/images/tables/HTML
  остаются следующим этапам. Source-only syntax сохраняется, не сериализуется из UI.
- Input, включающий punctuation, может после безопасного escaping разбиться
  MD4C на несколько runs. Их границы остаются защищёнными; parser rejection не
  превращается в обходную прямую замену source.
- Cursor mapping вне mapped текста приблизительный с объяснением; undo хранит
  позицию, не полное выделение. Full snapshots/full parse без large-file guarantees.
- GUI tests — offscreen/injected dialogs. Визуальная ручная приёмка и remote CI
  для этих изменений не выполнялись. Коммита/push нет.
