# Task 049 — Shortcut settings: action catalog and defaults

## Статус и владелец

- Статус: `Ready`.
- Владелец: архитектор/координатор; `cpp-developer`, `test-writer`.
- Приоритет: P1 (release blocker по owner QA).
- Источник: [release checklist, Settings](../release_checklist.md#settings).

## Контекст

Вкладка Shortcut показывает пустой/неинформативный текстовый редактор
override-строк. Пользователь не знает, какие actions существуют и какие
shortcuts назначены по умолчанию, поэтому не может осмысленно настроить
сочетания.

## Цель

Сделать Shortcut tab понятным каталогом действий с их описаниями, default
shortcuts и возможностью override, сохранив существующую валидацию конфликтов.

## Область работ

**Включено:**

- Каталог всех configurable actions (objectName), читаемые названия/группы и
  текущие/default shortcuts.
- UI-таблица или список, где пользователь видит действие, default и custom
  shortcut; возможность изменить/сбросить override.
- Использование единого action registry/SettingsService, чтобы список не
  расходился с реальными QAction.
- Сохранение/загрузка, конфликт-валидация, неизвестные actions и повреждённые
  значения — безопасные ошибки.
- Тесты на каталог/defaults/override/reset/validation и обновление README.

**Не включено:**

- Новые actions вне существующего меню/toolbar.
- Полная система редактирования shortcuts с записью клавиш (key capture), если
  она не нужна для минимального usable UI.
- Изменение default shortcuts без согласования.

## Ограничения и предположения

- Текущий формат `settings.json` и совместимость сохраняются.
- Отображение default shortcut не должно создавать override, пока пользователь
  его не изменил.
- Нельзя допускать неизвестные action names или конфликты; ошибки явные.

## Критерии приёмки

1. Shortcut tab перечисляет все actions, доступные для настройки, с читаемыми
   названиями и default shortcuts.
2. Пользователь видит, какие shortcuts изменены относительно defaults.
3. Override/reset сохраняется и применяется после restart/apply по текущему
   контракту Settings.
4. Конфликты/неизвестные значения отклоняются с понятной ошибкой; defaults
   восстанавливаются безопасно.
5. Тесты покрывают каталог, defaults, override, reset и validation; README
   обновлён.

## План проверок

- Qt Test для SettingsService/action registry и Shortcut tab UI.
- GUI test setValues/collect для default и override.
- Windows Debug/Release и Ubuntu Debug/Release/ASan CTest.

## Зависимости и открытые вопросы

- Зависимость: Task 009.
- Нужно подтвердить у owner, нужен ли key-capture control или достаточно
  таблицы с текстовым вводом сочетания; какие actions считать configurable.

## Исполнительский промпт

Построй единый реестр configurable actions с названиями и default shortcuts,
покажи его в Shortcut tab, добавь override/reset и сохрани текущую validation.
Не создавай override для нетронутых defaults; неизвестные actions и конфликты
должны явно отклоняться. Покрой тестами и обнови README.

## Итог выполнения

Ожидает реализации.
