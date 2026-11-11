# Task 029 — Инертные изображения и редактирование alt-подписей

## Статус и владелец

- Статус: `Done`. Родитель: [005](task_005.md). Зависимости: 026, 027, 028.
- Владелец: архитектор + C++ разработчик.

## Контекст

Continuous WYSIWYG показывает image как полностью защищённый placeholder,
хотя source-preserving ReplaceText уже умеет менять локальный normal text с
полным AST guard. Следующий безопасный срез раскрывает alt-подпись изображения
без загрузки ресурса, навигации и изменения src/title/reference binding.

## Цель

Показывать реальные alt-подписи images и безопасно менять локальный текст
подписи в общей Session, сохраняя image source bytes, адрес, title, definition
и все остальные исходные байты.

## Область работ

- Rendering: image children отображаются как стилизованный inert label; QTextCharFormat
  не получает image/anchor/href; resources не загружаются.
- Alt editing: normal mapped text run внутри image alt редактируется прежним
  ReplaceText в пределах одного run. Formatting children остаются protected для
  новых format-команд, но существующие strong/em/strike отображаются.
- Images внутри link: link остаётся inert; alt-подпись редактируется, href и
  image src/title не меняются.
- Autolinks не относятся к image; image source schemes (javascript/data/file)
  остаются инертными.
- Images/code/HTML/entity fragments внутри alt, cross-run/empty/newline edits и
  stale token остаются protected/rejected по прежним правилам.
- Source-only reference definitions не пересериализуются; reference image
  сохраняет spelling и binding.

## Ограничения

- Не создавать/удалять изображения, не менять src/title и не открывать URL.
- Не добавлять native image formats, resource loading или rich clipboard.
- Не реализовывать table/code layout в этом срезе.

## Критерии приёмки

1. `![before](/image "T")` → `![after](/image "T")` через реальную UI-правку.
2. `![before][id]` → `![after][id]`; definition bytes/URL/title и старые uses
   прежние.
3. Image внутри link `[![logo](file:///private)](/keep)` редактируется только
   alt; href/src/title прежние, click/navigation не вызывается.
4. `javascript:`/`data:`/`file:` image sources остаются инертными.
5. Strong/em/strike/image/code/entity fragments внутри alt не получают новых
   formatting commands; cross-run/empty/newline/stale edits отвергаются.
6. Switch/Save/undo/redo/dirty/checkpoint и SDI-независимость точны.
7. Engine и GUI fixtures покрывают success/rejection и обе платформы.

## Зависимости и открытые вопросы

- Зависимости: 026–028 Done. Новые URL/resource policies не вводятся.
- Полная Task 005 остаётся In Progress; создание/удаление images и изменение
  адресов — вне среза.

## Исполнительский промпт

Версия 1. Реализуй только этот срез в `continuous_editor` и существующих Qt
Test. Рендери alt children вручную QTextCursor, не создавай image formats или
anchors, не загружай ресурсы. Normal mapped alt runs идут через прежний
ReplaceText/Session; image attributes и definitions не меняй. Не добавляй
format commands внутри alt. При ошибке восстанови projection и сообщи причину,
без fallback на прямую замену source. Fixtures — данные; navigation handlers
используй только как recording, browser не запускай. Проверь engine/GUI,
Windows/Linux CTest и Linux sanitizers; отчёт — здесь.

## Итог выполнения

### Реализация

- `ContinuousEditor::renderInline` теперь обрабатывает `MD_SPAN_IMG` как
  стилизованный inert label: alt children рендерятся вручную, foreground/
  background/tooltip обозначают image, но `QTextCharFormat::isImageFormat`,
  anchor/href и resource loading не используются.
- Normal mapped text внутри alt проходит через существующий `ReplaceText` и
  общий Session/applyEdit guard. Src/title/image attributes и reference
  definitions в источнике не меняются; unsafe URI schemes остаются инертными.
- Formatting spans (strong/em/strike) внутри alt отображаются, но новые
  format-команды внутри alt не открыты. Images/code/HTML/entity fragments
  внутри alt остаются protected.
- Notice/tooltips обновлены: links и images inert; editing ограничен
  label/alt; addresses/sources/titles — в Markdown.
- Добавлены engine fixtures для inline/reference/unsafe images, empty/newline/
  CRLF rejection и сохранения src/title/definition. GUI test проверяет
  inline/reference/nested-link image alt edits, undo/dirty и отсутствие
  navigation/resource formats.

### Проверки

- Windows UCRT64 Debug: build, **CTest 7/7 passed**; MD4C Qt Test **233
  passed**; GUI smoke **74 passed**.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build with the MD4C spike,
  CTest **7/7 passed**.
- Ubuntu sanitized Debug: ASan/UBSan/leaks with the MD4C spike, CTest **7/7
  passed**.
- `git diff --check --ignore-space-at-eol` прошёл; сборка без warnings.
  Google clang-format в среде отсутствует; dry-run не выполнялся.
- Native visual QA, clang-tidy и remote CI не запускались.

### Ограничения

- Создание/удаление images, изменение src/title и открытие URL не реализованы.
- Autolinks и ссылки остаются inert; image resource loading отключён.
- Cross-run/empty/newline edits и новые format commands внутри alt
  отклоняются; code/HTML/entity fragments внутри alt защищены.
- Полная Task 005 остаётся In Progress; code/table/blockquote rendering и
  соответствующие structural commands остаются следующими срезами.
- Создан локальный коммит; push не выполнялся.
