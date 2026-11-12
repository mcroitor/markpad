# Task 011 — Сквозная проверка и готовность к выпуску

## Статус и владелец

- Статус: `In Progress` (решения по unblock приняты; ветка `feature/task_011` от `main`)
- Владелец: архитектор/координатор; тестировщик и DevOps по необходимости
- Приоритет: P2

## Контекст

Markpad должен работать на Windows и Linux, открывать/сохранять GFM и предоставлять три редактируемых режима, HTML/PDF export, page settings и пользовательские настройки. Unit и component tests не заменяют проверку полного пользовательского цикла.

## Цель

Провести сквозную проверку интегрированного приложения, закрыть функциональные пробелы и подготовить воспроизводимый checklist готовности к выпуску без самостоятельного выбора неутверждённых каналов поставки.

## Область работ

**Включено:**

- End-to-end сценарии: New/Open/Save/Save As, переключение трёх режимов, форматирование GFM, закрытие с unsaved changes, многооконность, настройки и HTML/PDF export.
- Проверка доступности ключевых команд, явности ошибок и отсутствия потери данных.
- Проверка Windows UCRT64/GCC и Linux/Ubuntu/GCC CI.
- Сверка README, sketch, AGENTS, architecture и task статусов с фактическим поведением.
- Запись известных ограничений, совместимых версий и инструкций для разработчиков.

**Не включено:** installer/signing/update channel; внедрять их только по отдельному требованию.

**Принятые решения для unblock (2026-11-11):**

- Remote CI: push `feature/task_011` и открыть PR; GitHub Actions — обязательное evidence.
- Visual QA: автоматизированные Windows/Linux снимки + ручная sign-off владельца.
- Print: проверить QPrinter/PDF и Microsoft Print to PDF; physical-printer и Linux CUPS   ограничения документировать, не выдавать за выполненные.
- Feature gaps: текущие задокументированные ограничения (nested code/table, HTML,   link/image address/title, rich/multiline cells, syntax highlighting) не блокируют   этот релиз.
- Deliverable: Windows portable binary с Qt runtime и лицензиями; installer/signing   остаются вне scope.

## Ограничения и предположения

- Выполнять после завершения зависимостей.
- Не заявлять выпускную готовность при падающих обязательных проверках или открытом риске потери данных/активного HTML.
- Любой незакрытый scope/feature gap согласовать с владельцем продукта; не расширять требования в рамках стабилизации.

## Критерии приёмки

1. Основные E2E сценарии проходят на Windows и Linux либо платформенное ограничение конкретно задокументировано.
2. Обязательные CI build/test jobs проходят.
3. Документы соответствуют реализованному поведению; все известные дефекты/ограничения имеют владельца или явно отложены.
4. Есть краткий выпускной checklist и перечень не входящих в текущий релиз решений.

## План проверок

- Запустить полные CTest suites и workflow обеих платформ.
- Пройти ручной checklist UI/файловых операций/режимов/экспорта.
- Сравнить paginated mode и PDF на фиксированных fixtures структурно; pixel-perfect   equality не является критерием.
- Проверить Windows Release portable package с Qt runtime, `MD4C-LICENSE.md` и   запуском без Qt из `PATH`.
- Проверить Microsoft Print to PDF; physical-printer и Linux CUPS явно записать как   limitations, если они не выполнялись.
- Собрать screenshots и провести owner sign-off по   [release checklist](../release_checklist.md).

## Зависимости и открытые вопросы

- Зависимости: Tasks 004, 005, 006, 007, 008, 009.
- Открытые вопросы по установщикам/подписи/каналам обновления не блокируют эту задачу и остаются вне текущего объёма.

## Исполнительский промпт

Проведи Task 011 после завершения всех зависимостей. Используй только утверждённые требования и зафиксированные команды сборки. Выполни CI и end-to-end checklist на Windows и Linux, включая сохранность данных, переключение GFM режимов, export и пагинацию. Проверь синхронность README/sketch/AGENTS/ARCHITECTURE с продуктом. Не исправляй крупные несогласованные требования молча: заведи/обнови отдельные task-файлы и запроси решение владельца, если критерии не определены. В итоговом отчёте дай результаты по каждой платформе, список команд, известные дефекты и решение о готовности; не утверждай выпускную готовность при непрохождении обязательных ворот.

## Итог выполнения

- Dependency Task 005 снята: continuous scoped объём принят Task 042.
- Создана ветка `feature/task_011` от `main` (`6df04da`), зафиксированы решения   по unblock и release checklist: [docs/release_checklist.md](../release_checklist.md).
- Локальные ворота пройдены: Windows Debug/Release CTest 7/7, Ubuntu   Debug/Release CTest 7/7, Linux ASan/UBSan/leaks CTest 7/7; MD4C suite входит   в CTest. Windows portable package собран, содержит Qt/GCC/runtime DLL,   `LICENSE`, `MD4C-LICENSE.md` и каталог лицензий; запускается без Qt в `PATH`.
- Добавлены Windows screenshots light/dark и Settings dialog в   [docs/qa/task_011](../qa/task_011/README.md). Visual QA выявил недостаточный   контраст dark WYSIWYG; цвета сделаны palette-aware, добавлен runtime   re-render и regression test `continuousDarkThemeColors`.
- Microsoft Print to PDF выполнен: получен читаемый A4 PDF. Paged view показал   3 страницы, print/PDF — 2 страницы; это известное ограничение Task 008   (PageLayoutEngine vs Qt text layout), требуется owner sign-off.
- Remote CI green: run `43155710816` на commit `d769c68`; Linux Debug/Release,   Linux ASan/UBSan, Windows Debug и Windows Release portable прошли. CI artifact   `markpad-0.1.0-windows-ucrt64` содержит 176 файлов, `markpad.exe` SHA-256   `5162839752ea55857f97caf35f89b1fd78ff4b832eabdfa49ed47b9157fc6676`,   запускается без Qt в `PATH`. PR #7 был слит в `main` на `ff535a1`; дальнейшие   QA-исправления и Task 043 находятся в ветке `feature/task_043`.
- QA follow-up (2026-11-12): исправлена подсветка `**bold**` в Markdown-редакторе   (больше не отображается как italic); Settings dialog переведён на вкладки   General/Editor/Page/Shortcut и покрыт GUI-тестом.
- WYSIWYG typography settings реализованы в [Task 043](task_043.md) (per-block   controls на Page); настоящая постраничная поверхность выделена в   [Task 044](task_044.md) и остаётся release-блокером до реализации.
- Additional Windows release QA: HTML export через native export dialog из   portable package создаёт корректный файл; oversized single-block fixture   показывает `Overflow warning` в paged status bar. Evidence — в   `docs/qa/task_011/release-qa` и `docs/qa/task_011/overflow`.
- Native File Open/Save/Save As, unsaved-close Save/Discard/Cancel и multi-window   SDI проверены на Windows portable package; evidence — `saved-copy.md`,   `unsaved-close.png`, `close-save.md`. Автоматические тесты сохранения/закрытия/SDI   также проходят.
- Owner QA (2026-11-12) выявил dark-theme menus/Settings, WYSIWYG backgrounds/code/table fit, PDF layout, toolbar interaction и Shortcut tab gaps. Созданы задачи [045](task_045.md)–[049](task_049.md); paged layout остаётся [Task 044](task_044.md), PDF parity — [Task 047](task_047.md).
- Physical printer, Linux CUPS, Linux native visual QA, owner visual sign-off и   явное принятие paged/PDF page-count divergence остаются незакрытыми.
- PR не создан: GitHub CLI token не имеет permission на создание PR; ветка   `feature/task_011-followup` запушена, CI прошёл.
- Решение о готовности: `In Progress`/не заявляется до owner sign-off,   Tasks 044–049 и оставшихся ручных проверок; автоматические ворота и functional   dependency 005 больше не блокируют задачу.

## Update 2026-11-12: Task 044 page sheets

Task 044's Option A page-sheet implementation is complete locally: centered
page sheets, desk gaps, page numbers, source/token-preserving page-break
spacing, and atomic table/fenced-code placement. Evidence is in
`docs/qa/task_044/`. Task 011 remains `In Progress` until owner visual sign-off
and Task 047 PDF-parity/owner acceptance are complete.