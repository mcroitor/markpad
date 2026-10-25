# Task 004 — SDI-окна и Markdown-редактор

## Статус и владелец

- Статус: `Done`
- Владелец: архитектор + C++ разработчик
- Приоритет: P1

## Контекст

Markpad — SDI: одно окно соответствует одному документу, несколько документов открываются в разных окнах. Sketch задаёт меню File/Edit/Format/View/Help, toolbar, статусную строку, команды открытия/сохранения и Markdown-режим с подсветкой. При закрытии с изменениями требуется защита от потери данных.

## Цель

Добавить минимальный рабочий Qt desktop интерфейс для исходного Markdown и жизненного цикла документов в SDI.

## Область работ

**Включено:** Application Controller/window lifecycle, пустое New, Open/Save/Save As, Markdown editor/syntax highlighting, undo/redo, dirty title, close confirmation, базовая статусная строка, command-line/open-file handoff в отдельное окно.

**Не включено:** WYSIWYG, экспорт, настройки кроме временно необходимых UI defaults.

## Ограничения и предположения

- Использовать Qt Widgets; окно не содержит вкладок документов.
- Каждое окно владеет независимой Document Session.
- Владелец утвердил: Open переиспользует только чистое пустое безымянное окно,
  иначе создаёт отдельное. New всегда создаёт новое окно.
- При Save cancel/failure закрытие окна отменяется.
- Не добавлять форматирование, не представимое в GFM.

## Критерии приёмки

1. New/Open создают/открывают отдельные окна согласно утверждённой политике.
2. Каждое окно независимо отображает и сохраняет свой документ; нет вкладок.
3. Dirty state отражается в заголовке/командах, close prompt не теряет изменения.
4. Markdown editor поддерживает редактирование, undo/redo и подсветку синтаксиса без изменения содержимого сам по себе.
5. GUI-интеграционные тесты покрывают несколько окон и отмену закрытия.

## План проверок

- CTest ядра и GUI tests Qt Test (offscreen где применимо).
- Ручная проверка открытия нескольких файлов, New/Open в чистом/занятом окне, отмены Save/Close на Windows.
- Linux CI smoke test GUI под виртуальным/offscreen platform plugin, если доступно.

## Зависимости и открытые вопросы

- Зависимости: Tasks 001 и 003.
- Политика Open/New согласована; Task 003 объединена в main.

## Контракт UI v1

- Каждое окно владеет собственной Session и undo history. Controller владеет
  жизненным циклом окон, обрабатывает CLI paths и QFileOpenEvent. Ошибка открытия
  не создаёт пустое лишнее окно и не меняет старый документ.
- Save/Save As используют encoding/BOM Task 003; диалог Save As предлагает пять
  кодировок и BOM. Open сначала autodetect; при encoding error позволяет явно
  выбрать encoding и повторить, отмена не меняет состояние.
- Закрытие dirty: Save/Discard/Cancel; Cancel или save error/cancel отменяют close.
  File/Exit закрывает окна через тот же guard, без обхода.
- Markdown source — QPlainTextEdit, plain text only. Подсветка визуальная,
  bounded базовыми GFM constructs (headings/lists/quotes/code/links/emphasis),
  не полноценный parser и не гарантия GFM conformance.
- Source Session канонический: загрузка и подсветка не меняют исходные bytes.
  UI адаптирует paragraph separators и сохраняет исходные EOL вне редактирования.
  Новые line breaks используют первый EOL source, иначе LF; undo возвращает
  точные source bytes, dirty вычисляет Session, не QTextDocument::isModified.
- Save error показывается явно, path/checkpoint/dirty не сбрасываются. Безопасность:
  HTML только текст, никакой загрузки внешних ресурсов.
- Пока English UI как существующий bootstrap, без новой localization subsystem.
  Тесты используют injected dialogs, без автоматизации native modal dialogs.

## Исполнительский промпт

Версия 2. Реализуй UI-контракт этой карточки поверх Session Task 003.
Входы: src/main*, document_session*, Qt file adapter, CMake и существующий Qt Test.
Создай небольшой Controller, MainWindow и visual highlighter; не включай MD4C
в UI и не создавай второй канонический документ. Widgets — только projection.
Обязательно проверь independent windows, New/Open policy, CLI/file-open handoff,
dirty title, точные mixed EOL/Unicode/undo bytes, save/open cancellation/errors,
Save/Discard/Cancel при close, syntax highlight без source mutation.
Не исполняй HTML/fixtures, не добавляй export/WYSIWYG/settings/dependencies.
Ожидаемые source/state фиксируй независимо от output. При противоречии запроси
уточнение, не меняй политику молча. Выход: код/GUI Qt Test offscreen, фактические
Windows/Linux build/CTest и ограничения ручных проверок в итог этой карточки.
Отчёт: файлы, scenarios expected/actual, commands/results, блокеры. Полный source
snapshot/history допустим для простоты, performance больших файлов не обещать.

## Итог выполнения

- Реализованы `ApplicationController`, SDI `MainWindow`, injectable
  `DocumentDialogs`, visual `MarkdownHighlighter` и CLI startup.
  Session/file adapter Task 003 подключены к UI без зависимости от MD4C.
- New/Open следуют утверждённой политике. Окна независимы, ошибки Open не создают
  лишнее окно. Save/Save As сохраняют encoding/BOM; decode errors допускают
  explicit encoding retry. Close/Exit защищены Save/Discard/Cancel.
- Editor поддерживает plain-text ввод, меню/контекстное меню, keyboard shortcuts,
  source undo/redo, dirty title и строку состояния. Подсветка не меняет source.
  Qt-native undo отключён, клавиатура маршрутизируется в source history.
- Точный диапазон `contentsChange` используется вместо diff видимого текста:
  замена выделенного LF корректно заменяет соответствующий CR/CRLF, даже если
  новый отображаемый LF совпадает с прежним на краю заменяемого фрагмента.

### Проверки

- Windows UCRT64 Debug: configure/build и CTest с
  `-DMARKPAD_BUILD_MD4C_SPIKE=ON` — 3/3 passed.
- Windows native process smoke: приложение создало окно `Untitled - Markpad`,
  приняло запрос закрытия чистого окна и завершилось с exit code 0.
- Ubuntu WSL Debug: configure/build и тот же CTest — 3/3 passed.
- Ubuntu WSL, отдельная Debug-сборка с
  `-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined`
  и `-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined`:
  document + GUI CTest — 2/2 passed, без отключения leak detection.
- Воспроизведение: стандартные команды README с указанными flags; CTest
  `markpad_smoke` запускает GUI integration suite с `QT_QPA_PLATFORM=offscreen`.
- `clang-format --dry-run --Werror --style=Google` и `git diff --check` успешны.
- GUI scenarios: несколько окон, New/Open reuse, неуспешное Open и отмена,
  явная кодировка, mixed EOL/Unicode/NBSP/separators/literal BOM, Unicode range
  replacement, source-preserving Save, undo/redo/branching и keyboard shortcuts,
  NUL rejection, Save As UTF-16+BOM, save failure без изменения checkpoint,
  Close Save/Discard/Cancel, Exit cancel, file-open event/CLI paths,
  highlighting без изменения source/revision. Expected bytes заданы fixtures.

### Ограничения и оставшиеся проверки

- Native modal dialogs и визуальная ручная проверка Windows/Linux ещё не выполнены:
  automated tests используют injected dialogs/offscreen. Удалённый CI не запускался.
- Подсветка — базовые эвристики, не полный GFM parser. WYSIWYG/export/settings
  не добавлены. HTML остаётся текстом, внешние ресурсы не загружаются.
- History — полные source snapshots на каждое изменение, без coalescing слов
  или гарантий производительности больших файлов. Cursor restoration позиционное,
  выделение не восстанавливается.
- Exit закрывает окна последовательно, отмена не переоткрывает уже закрытые.
  OS associations и single-instance IPC не входят в реализацию.
- Блокеров реализации нет; публикация/PR остаются за владельцем.
