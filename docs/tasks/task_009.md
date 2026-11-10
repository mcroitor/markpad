# Task 009 — Настройки приложения и сочетания клавиш

## Статус и владелец

- Статус: `Done` (решения Task 010 приняты; core settings + page defaults)
- Владелец: C++ разработчик; архитектор подтверждает настройки
- Приоритет: P2

## Контекст

Sketch предусматривает настройки языка, темы, editor font, line numbers, word wrap, default file location и пользовательские shortcuts. Конфигурационный файл должен валидироваться и не блокировать запуск при отсутствии/повреждении.

## Цель

Реализовать устойчивое хранение и применение пользовательских настроек, кроме Auto Save и page setup, которые вынесены отдельно.

## Область работ

**Включено:** Settings Service, default values, schema version, конфигурационный путь Windows/Linux, настройка theme/language/editor behavior/default file location, customizing shortcuts, migration/invalid setting handling, глобальные page-setup defaults (A4 portrait/20 мм) для Task 006.

**Не включено:** Auto Save, per-document page metadata, полноценная локализация всех строк без перевода.

## Ограничения и предположения

- Сохранять настройки как платформенно-подходящий текстовый config без секретов.
- Применять корректные default values при отсутствии/ошибке; повреждение должно быть диагностировано.
- Не принимать shortcut mapping, конфликтующий с обязательными системными/редакторскими командами, без понятной валидации.

## Критерии приёмки

1. Настройки сохраняются и применяются после перезапуска.
2. Отсутствующий/частично повреждённый/неподдерживаемой версии config не ломает запуск и даёт диагностируемый fallback.
3. Пользовательские shortcuts отображают конфликт/невалидное значение до применения.
4. Qt-независимые unit tests для parsing/validation, GUI tests для применения.

## План проверок

- Tests на defaults, round-trip, повреждённый config, миграцию schema, конфликт shortcut.
- Проверить Windows/Linux config path и права записи.
- CTest и ручное изменение нескольких настроек с перезапуском.

## Зависимости и открытые вопросы

- Зависимости: Tasks 001 и 004.
- Решения Task 010: config — пользовательский Markpad root (`AppConfigLocation`,
  Windows `%APPDATA%/Markpad`, Linux `~/.config/Markpad`); English default,
  дополнительные языки — `lang/*.ini` с fallback English; page-setup defaults
  глобальные (A4 portrait/20 мм, configurable); Auto Save отложен.

## Исполнительский промпт

Реализуй Task 009 только после уточнения блокеров и статуса `Ready`. Создай Settings Service с версионируемой схемой, валидируемыми значениями и тестируемыми defaults. Используй платформенно-подходящие user config paths; не записывай данные в каталог установки. Настройки theme, editor preferences, default file location и shortcuts должны применяться после перезапуска. Не включай Auto Save/page setup. Покрывай ошибочные файлы и shortcut conflicts тестами. Не глотай ошибки молча; сообщай диагностически и используй документированные safe defaults.

## Итог выполнения

### Реализация

- Добавлены `src/settings.{h,cc}` и target `markpad_settings`: versioned JSON
  settings schema 1, defaults, validation, partial-invalid fallback и
  диагностика. Config root — `QStandardPaths::AppConfigLocation` для Markpad
  (`%APPDATA%/Markpad`, `~/.config/Markpad`), файл `settings.json`.
- Настройки: language, theme, editor font family/size, word wrap, default file
  location, shortcuts и global PageSetup (A4/Letter/Legal, portrait/landscape,
  четыре margins). Default: A4 portrait, 20 мм, English, system theme, 12 pt,
  wrap on.
- Language: встроенный English fallback + `lang/*.ini` в config root; неверный
  язык откатывается на English с diagnostic. UI меню/actions используют
  language map; смена языка применяется после перезапуска.
- Shortcuts: validation QKeySequence, duplicates и reserved standard commands
  отклоняются. Settings dialog показывает ошибку до accept; MainWindow
  применяет валидные mapping к actions по objectName после перезапуска.
- `SettingsDialog`: группы General (Language, Theme, Auto Save deferred,
  Default file location), Editor (Font size, Font family, Line numbers deferred,
  Word wrap), Page (size/orientation/margins) и Shortcuts; OK валидирует
  значения до применения.
- Menu bar приведён к [sketch.md](../../sketch.md): File (New, Open, Save,
  Save As, Export→PDF/HTML, Print, Exit), Edit (Undo/Redo/Cut/Copy/Paste,
  Settings), Format (Heading H1–H6, Bold/Italic/Strikethrough, Blockquote,
  Code, Horizontal Line, Link, Image, Table, List→Ordered/Unordered), View
  (Full Screen, Zoom In/Out/Reset, Switch Mode), Help. Недоступные сейчас
  действия disabled с tooltip; PDF/Print зависят от Task 008.
- Убраны лишние относительно sketch пункты: Close, Select All, Status bar,
  About; Task/Paragraph остаются доступны через context menu, чтобы не терять
  возможности Tasks 026–027 при точном menu bar.
- Toolbar повторяет sketch: New/Open/Save, Undo/Redo/Settings, Heading,
  Bold/Italic/Strikethrough, Blockquote/Code/Horizontal Line/Link/Image/Table,
  List/Ordered/Unordered, Switch Mode.
- Status bar показывает Characters/Words/Lines и Page N of M в paged mode
  вместо прежней Ln/Col-индикации.
- Menu/toolbar/status structure закреплена GUI test `sketchMenuStructure`.
- MainWindow загружает settings при старте, применяет palette/font/wrap/default
  location/shortcuts, показывает диагностику повреждённого config в status bar.
  Default file location передаётся DocumentDialogs.
- Добавлен `lang/en.ini` как шаблон language-файла.
- Auto Save остаётся отложенным; per-document page metadata не вводится.

### Проверки

- Windows UCRT64 Debug: build, **CTest 6/6 passed**; `markpad_settings`
  **8 passed**; `markpad_layout` **9 passed**; GUI smoke suite **75 passed**;
  MD4C suite **232 passed**.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build, CTest **6/6 passed**.
- Ubuntu sanitized Debug: ASan/UBSan/leaks, CTest **6/6 passed**.
- `git diff --check --ignore-space-at-eol` прошёл; сборка без warnings.
  Google clang-format в среде отсутствует; dry-run не выполнялся.
- Native visual QA, clang-tidy и remote CI не запускались.

### Ограничения

- Line-number gutter не реализован по решению владельца (core settings slice).
- Полная локализация всех строк не выполнена: language map применяется к
  основным menu/action labels; смена языка требует перезапуска. Fallback
  English гарантирован.
- Auto Save и per-document page metadata не реализуются; page setup —
  глобальные defaults для Task 006.
- Config migration реализована только как safe fallback для schema > 1;
  будущие версии должны добавить явные миграции.
- Коммит/push не создавались.
