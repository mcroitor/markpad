# Task 032 — Иконки кнопок toolbar

## Статус и владелец

- Статус: `Done`.
- Владелец: архитектор + C++ разработчик.
- Родитель: [Task 009](task_009.md). Зависимости: 009, 030, 031.

## Контекст

Toolbar уже содержит действия File/Edit/Format/View, но использует
`Qt::ToolButtonTextOnly` и не задаёт ни одной иконки. Требование пользователя —
кнопки toolbar должны быть с иконками. В проекте нет ресурсных изображений и
зависимости Qt SVG; добавлять внешний icon pack не требуется.

## Цель

Сделать toolbar компактным icon-button surface: каждая не-separator кнопка
получает понятную монохромную иконку, tooltip/action text сохраняются,
disabled-состояние визуально отличается, а цвета адаптируются к light/dark
теме без новых внешних зависимостей.

## Область работ

**Включено:**

- `src/toolbar_icons.{h,cc}` с программной отрисовкой QIcon через QPainter:
  File New/Open/Save, Undo/Redo, Settings, Heading, Bold, Italic,
  Strikethrough, Blockquote, Code, Horizontal Line, Link, Image, Table,
  List, Ordered, Unordered, Switch Mode.
- Toolbar использует `Qt::ToolButtonIconOnly`, единый icon size и tooltips.
- Иконки имеют Normal и Disabled pixmaps; цвет Normal берётся из текущей
  палитры (`QPalette::ButtonText`), Disabled — приглушённый.
- `MainWindow::refreshToolbarIcons()` перерисовывает иконки после применения
  темы в `applySettings`; object names/actions/menu structure не меняются.
- Qt Test: все toolbar actions имеют non-null icons, separators пропущены,
  icon-only style задан, object names/menu order не изменились, dark theme
  сохраняет non-null icons.

**Не включено:**

- Новые действия, изменение порядка/состава toolbar или меню.
- Внешние SVG/PNG assets, Qt SVG, icon packs, platform-specific resources.
- Изменение поведения Format/Edit/File actions, кроме вызова существующих
  actions через иконки.
- Иконки для menu-only действий, context menu и вкладок.
- HiDPI-специфичные bitmap variants сверх масштабирования QIcon.

## Ограничения и предположения

- Иконки рисуются в 24×24 с antialiasing и прозрачным фоном; QIcon
  масштабируется Qt под фактический icon size.
- Цвета Normal/Disabled зависят от текущей палитры; после смены темы иконки
  перерисовываются.
- `Qt::ToolButtonIconOnly` не скрывает action text: он остаётся доступным в
  меню, tooltip и accessibility name.
- Object names и порядок toolbar actions сохраняются, чтобы существующие
  shortcut/settings/tests contracts не сломались.
- Иконки не должны быть единственным носителем смысла: tooltips сохраняются.
- Новые зависимости и бинарные assets не добавляются.

## Критерии приёмки

1. Каждая non-separator toolbar action имеет non-null icon в Normal и Disabled
   modes; separators не получают icon.
2. Toolbar style — icon-only; tooltips/action text для всех кнопок непустые.
3. Object names, порядок кнопок и menu structure не изменились относительно
   `sketch.md` contracts.
4. Light, dark и system palette дают видимые иконки; после `applySettings`
   иконки не становятся blank/black-on-black.
5. Disabled Code/Link/Image/Table action показывают disabled icon, enabled
   action — активную иконку.
6. Qt Test проходит на Windows/Linux; сборка без новых зависимостей.

## План проверок

- Qt Test: toolbar style, icon non-null для всех toolbar actions, separators,
  object names/order, tooltips, dark-theme refresh.
- Windows UCRT64 Debug: build, CTest, MD4C spike.
- Ubuntu WSL: normal и ASan/UBSan/leaks CTest.
- `git diff --check --ignore-space-at-eol`.
- Native visual QA и remote CI отдельно; если недоступны, указать явно.

## Зависимости и открытые вопросы

- Зависимости: существующий toolbar/menu contract Task 009; actions Task 030/031.
- Открытых блокирующих вопросов нет. Новые действия и assets не требуются.
- Полная Task 005 остаётся In Progress; Task 011 остаётся Blocked.

## Исполнительский промпт

Реализуй только toolbar icon presentation. Добавь программный icon factory,
подключи его к существующим actions через `MainWindow::refreshToolbarIcons`,
переключи toolbar на icon-only, сохрани все object names, порядок, tooltips,
shortcuts и menu structure. Не добавляй внешние assets, Qt SVG, новые actions
или изменение поведения. Иконки должны иметь Normal/Disabled pixmaps и
перерисовываться после смены темы. Добавь Qt Test на icon presence/style/order
и выполни Windows/Linux tests. Отчёт о выполнении добавь сюда.

## Итог выполнения

### Реализация

- Добавлен `src/toolbar_icons.{h,cc}` с программной отрисовкой QIcon через
  QPainter (24×24, antialiasing, прозрачный фон). Поддержаны все 20
  non-separator toolbar actions: File New/Open/Save, Undo/Redo, Settings,
  Heading, Bold/Italic/Strikethrough, Blockquote, Code, Horizontal Line, Link,
  Image, Table, List/Ordered/Unordered и Switch Mode.
- Для каждой иконки создаются Normal и Disabled pixmaps; Normal использует
  текущий `QPalette::ButtonText`, Disabled — тот же цвет с прозрачностью.
- MainWindow переключил main toolbar на `Qt::ToolButtonIconOnly` с icon size
  20×20 и заполняет tooltips из action text. Object names, порядок действий,
  shortcuts и menu structure не изменены.
- `MainWindow::refreshToolbarIcons()` вызывается после построения toolbar и
  после `applySettings`, поэтому light/dark/system palette перерисовывают
  иконки.
- Добавлен Qt Test `toolbarIcons`: проверяет icon-only style, размер, non-null
  Normal/Disabled pixmaps, tooltips, 20 кнопок, ключевые menu actions и
  обновление после dark theme.

### Проверки

- Windows UCRT64 Debug: build, **CTest 7/7 passed**; MD4C Qt Test **266
  passed**; GUI smoke **77 passed**.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build with the MD4C spike,
  CTest **7/7 passed**.
- Ubuntu sanitized Debug: ASan/UBSan/leaks with the MD4C spike, CTest **7/7
  passed**.
- `git diff --check --ignore-space-at-eol` прошёл. Native visual QA,
  clang-format, clang-tidy и remote CI не запускались.

### Ограничения

- Иконки монохромные программные, без внешних assets и без отдельных HiDPI
  bitmap variants; QIcon масштабирует 24×24 pixmap до 20×20.
- Иконки добавлены только toolbar actions; menu-only/context-menu actions
  остаются без иконок.
- Toolbar остаётся icon-only, поэтому смысл кнопки передают tooltip и action
  text в меню/accessibility; визуальная ручная проверка не выполнялась.
- Новые действия, порядок toolbar и меню не менялись.
- Создан локальный коммит; push не выполнялся.
