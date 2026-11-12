# Task 045 — Dark theme readability

## Статус и владелец

- Статус: `Ready`.
- Владелец: архитектор/координатор; `cpp-developer`, `test-writer`.
- Приоритет: P1 (release blocker по owner QA).
- Источник: [release checklist, Dark theme](../release_checklist.md#dark-theme).

## Контекст

В dark theme пункты меню отображаются тёмным текстом на тёмном фоне, а в
Settings часть подписей/контролов выглядит светлым текстом на светлом фоне.
Тема применяется глобально через `QApplication::setPalette`, но меню, popup-
контролы и часть виджетов Settings не получают согласованный контрастный
palette/style.

## Цель

Сделать меню, popup-меню, toolbar tooltips, Settings и остальные диалоги
читаемыми в light/dark/system theme без изменения пользовательских данных или
source-контракта.

## Область работ

**Включено:**

- Проверить и исправить palette/style для `QMenuBar`, `QMenu`, popup-контролов,
  `QToolTip`, Settings и стандартных QMessageBox/QFileDialog.
- Обеспечить контраст text/background и disabled-состояний в dark theme.
- Добавить regression-тест/визуальную проверку dark theme для меню и Settings.
- Обновить screenshots dark theme и release checklist.

**Не включено:**

- Новый движок тем, загрузка внешних стилей или произвольные QSS-темы.
- Изменение структуры Settings, кроме исправления отображения.

## Ограничения и предположения

- Light/system theme должны сохранить текущий вид.
- Исправление не должно менять shortcuts, Settings schema или документ.
- Платформенное поведение Qt-меню на Windows и Linux должно быть согласовано.

## Критерии приёмки

1. В dark theme все пункты меню и popup-меню читаемы; нет тёмного текста на
   тёмном фоне.
2. В Settings и диалогах все подписи, значения, disabled-контролы и кнопки
   читаемы; нет белого текста на светлых полях.
3. Переключение light/dark/system не требует перезапуска и не ломает
   существующий документ.
4. Автоматическая проверка контраста или стабильный screenshot test покрывает
   найденные случаи.
5. README/QA evidence обновлены; известные ограничения явно записаны.

## План проверок

- GUI test меню/Settings в dark theme.
- Визуальные screenshots light/dark на Windows; Linux CTest/offscreen.
- Windows Debug/Release CTest и Ubuntu Debug/Release/ASan.

## Зависимости и открытые вопросы

- Зависимость: Task 009 (Settings/theme).
- Открытый вопрос: какие именно меню/подписи пользователь считает нечитаемыми;
  приложить screenshots/пункты, если они не покрыты текущими QA captures.

## Исполнительский промпт

Воспроизведи dark theme, исправь palette/style для меню, popup-контролов,
Settings и диалогов, сохранив light/system поведение. Добавь regression test
и screenshots. Не скрывай оставшиеся нечитаемые элементы; перечисли их в отчёте.

## Итог выполнения

Ожидает реализации.
