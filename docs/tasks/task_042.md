# Task 042 — Acceptance matrix Task 005 и закрытие continuous-среза

## Статус и владелец

- Статус: `Done`.
- Владелец: архитектор/координатор + test-writer.
- Родитель: [Task 005](task_005.md). Зависимости: 026–041.

## Контекст

Tasks 026–041 реализовали все заявленные continuous-срезы: text/heading/task,
inline formatting, quotes/lists, code blocks, links, images, tables и
block-insertion commands. Осталось подтвердить родительские критерии единым
integration fixture, зафиксировать поддерживаемые границы и закрыть Task 005
без выдачи ручной visual/PDF/printer проверки за выполненную.

## Цель

Добавить acceptance matrix fixture для смешанного GFM-документа, подтвердить
source-preserving mode switch и protected placeholders для неподдержанных
вложенных конструкций, затем обновить Task 005 до `Done` с явными
ограничениями. Task 011 остаётся `Blocked` из-за нерушительных native/manual
release checks.

## Область работ

**Включено:**

- Один GUI integration fixture со смешанным документом:
  heading, paragraph, bold/italic/strike, inline code, quote, list, fenced
  code, link, image, top-level table, HR, raw HTML и nested table placeholder.
- Проверка continuous rendering, source token/source preservation, mode switch
  Markdown ↔ continuous ↔ Markdown.
- Проверка отсутствия изменения source при switch и наличии placeholders для
  HTML/nested table.
- Обновить Task 005: status `Done`, итог acceptance и явные limitations.
- Обновить Task 011: dependency Task 005 снята, status остаётся `Blocked` на
  native visual/PDF/printer/remote CI checks.

**Не включено:**

- Новые editing features, nested code/table editing, HTML rendering.
- Native visual QA, printer hardware, remote CI.
- Изменение production behavior ради теста.

## Ограничения и предположения

- Acceptance подтверждает реализованный top-level continuous scope; nested
  code/tables, HTML, link/image addresses/titles и syntax highlighting остаются
  явно документированными ограничениями.
- Task 005 можно закрыть, потому что неподдержанные конструкции защищены
  placeholders и не теряются, как требует Task 026 contract.
- Fixture не заменяет manual visual QA; Task 011 остаётся заблокированным до
  фактического native/manual прогона.

## Критерии приёмки

1. Смешанный документ рендерится в continuous без потери source/token.
2. HTML и nested table показаны protected placeholders, а не удалены.
3. Переключение continuous → Markdown → continuous сохраняет source/token и
   не создаёт dirty-изменений.
4. Acceptance fixture проходит Windows/Linux CTest и Linux sanitizers.
5. Task 005 отмечен `Done` с явными limitations; Task 011 не выдан за готовый
   релиз и остаётся `Blocked` из-за native/manual checks.

## План проверок

- GUI acceptance fixture.
- Windows UCRT64 CTest + MD4C spike.
- Ubuntu WSL normal/ASan/UBSan/leaks CTest.
- `git diff --check --ignore-space-at-eol`.
- Native visual/PDF/printer QA не запускались и явно остаются в Task 011.

## Зависимости и открытые вопросы

- Зависимости: 026–041.
- Открытых функциональных вопросов для заявленного continuous scope нет.
- Nested editing, rich table cells, link/image address/title editing и syntax
  highlighting остаются будущими задачами.

## Исполнительский промпт

Реализуй только acceptance fixture и документационное закрытие Task 005.
Не меняй production behavior и не добавляй новые функции. Fixture должен
проверить смешанный supported document, protected HTML/nested table и
source-preserving mode switch. Обнови Task 005/011 статусы честно: Task 005
Done с limitations, Task 011 Blocked на native/manual release checks. Выполни
Windows/Linux tests и sanitizers; отчёт добавь сюда.

## Итог выполнения

### Реализация

- Добавлен GUI acceptance fixture `continuousAcceptanceMatrix` со смешанным
  документом: heading/paragraph, bold/italic/strike, inline code, quote, list,
  fenced code, link, image, top-level table, HR, raw HTML и nested table.
- Fixture проверяет continuous rendering, source/token preservation,
  protected HTML/nested-table placeholders и round-trip continuous → Markdown
  → continuous без dirty-изменений.
- Task 005 переведён в `Done` для принятого top-level scoped объёма с явными
  limitations.
- Task 011 остаётся `Blocked`, но уже не из-за Task 005: остаются native
  visual/PDF/printer checks, remote CI и ручной release checklist.

### Проверки

- Windows UCRT64 Debug: build, **CTest 7/7 passed**; MD4C Qt Test **319
  passed**; GUI smoke **88 passed**.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build with the MD4C spike,
  CTest **7/7 passed**.
- Ubuntu sanitized Debug: ASan/UBSan/leaks with the MD4C spike, CTest **7/7
  passed**.
- `git diff --check --ignore-space-at-eol` прошёл. Native visual/PDF/printer
  QA, clang-format, clang-tidy и remote CI не запускались.

### Ограничения

- Acceptance относится к top-level continuous scope и не включает nested
  code/table editing, HTML rendering, link/image address/title editing,
  rich/multiline table cells или syntax highlighting.
- Native visual QA, printer hardware и remote CI остаются блокерами Task 011.
- Создан локальный коммит; push не выполнялся.
