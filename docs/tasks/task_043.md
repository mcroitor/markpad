# Task 043 — Настройки типографики WYSIWYG

## Статус и владелец

- Статус: `Done`.
- Владелец: архитектор/координатор; `cpp-developer` и `test-writer`.
- Приоритет: P1 (release blocker после QA).
- Решения владельца (2026-11-12): per-block style controls; размещение — вкладка Page.

## Контекст

Настройки Markdown-редактора уже задают семейство и размер шрифта, но
WYSIWYG-режим использует собственные жёстко заданные размеры заголовков и
системный monospace-шрифт для code blocks. Пользователь требует настраивать
типографику WYSIWYG, включая заголовки, код, цитаты и другие блоки.

## Цель

Добавить сохраняемые настройки типографики WYSIWYG и применять их к
непрерывному и постраничному режимам без изменения Markdown-исходника.

## Область работ

**Включено:**

- Персистентные поля `Settings` и обратно совместимая загрузка старых файлов.
- Элементы управления в существующих четырёх вкладках Settings (предпочтительно
  вкладка Editor; отдельная вкладка — только по решению владельца).
- Применение семейства/размера к body text, заголовкам H1–H6, code blocks,
  inline code и цитатам по выбранной модели.
- Обновление README/ARCHITECTURE и GUI/настройки тестов.

**Не включено:**

- Загрузка внешних font files, CSS/HTML-стилизация и произвольные пользовательские
  темы блоков.
- Изменение Markdown-исходника или формата GFM.
- Настоящая постраничная поверхность — Task 044.

## Ограничения и предположения

- Старые `settings.json` без новых полей должны загружаться с текущими
  значениями по умолчанию; отсутствие/повреждение файла не должно ломать запуск.
- Markdown-редактор сохраняет собственные font family/size.
- Решения о WYSIWYG-типографике не должны менять source/token/history contract.

## Выбранная модель

- **B: per-block style controls.** Отдельные настройки для body, заголовков,
  H1–H6, code и quote; контролы размещаются на вкладке Page.
- Семейство body/heading/code/quote, размеры, heading bold и quote italic
  сохраняются в `settings.json` и применяются к обоим WYSIWYG-режимам.

## Критерии приёмки

1. Пользователь меняет WYSIWYG font family/size в Settings, сохраняет и видит
   изменение в continuous и paged WYSIWYG после применения.
2. Заголовки, code blocks, inline code и цитаты используют выбранную модель;
   Markdown-исходник, token и undo/redo не меняются от смены стиля.
3. Некорректные/отсутствующие значения настроек дают безопасные defaults и
   диагностику, а не сбой.
4. Тесты покрывают persistence, применение к обоим WYSIWYG-режимам и отсутствие
   изменения source.
5. README и Settings UI отражают фактическое поведение.

## План проверок

- Qt Test для Settings load/save и backward compatibility.
- GUI-тест смены настроек и проверки форматов заголовка/кода/цитаты.
- Windows Debug/Release CTest и Ubuntu Debug/Release/ASan CTest.
- Визуальные screenshots light/dark.

## Зависимости и открытые вопросы

- Зависимости: Tasks 005, 009.
- Выбрано: per-block controls на вкладке Page; H1–H6 настраиваются отдельно.

## Исполнительский промпт

Реализованы per-block controls на вкладке Page по решению владельца; отдельный
промпт не требовался после уточнения модели.

## Итог выполнения

- Добавлены `WysiwygStyle` и поля body/heading/H1–H6/code/quote в Settings;
  старые `settings.json` загружаются с defaults.
- На вкладке Page добавлены font family/size controls для body, заголовков,
  code и quote, а также heading bold и quote italic.
- Continuous и paged WYSIWYG применяют эти стили; zoom масштабирует размеры.
- Добавлены Settings round-trip/fallback tests и GUI-тесты
  `continuousTypographySettings` / `settingsDialogUsesTabs`.
- Windows CTest 7/7; CI для предыдущего commit зелёный. Финальный CI для этого
  изменения запускается после push.
