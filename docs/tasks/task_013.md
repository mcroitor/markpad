# Task 013 — Вложенные диапазоны и общий контракт MD4C-правок

## Статус и владелец

- Статус: `Done`
- Владелец: архитектор + C++ разработчик
- Приоритет: P0

## Контекст

Task 012 ввела versioned heading edits только для top-level блоков; text/task
операции пока возвращают bytes без общего контроля снимка. До list/table mutations
нужно зафиксировать и проверить единый контракт.

## Цель

Связать узлы с родителями, расширить доказанные диапазоны на вложенные цитаты
и объединить text/task/heading edits в versioned-команду.

## Область работ

- Родительские связи и evidence envelopes контейнеров: это диапазоны известных
  дочерних bytes, не полные синтаксические диапазоны lists/quotes.
- Точные диапазоны однострочных P/ATX внутри явно маркированных цитат,
  включая несколько уровней. Префиксы контейнера сохраняются вне правки.
- Общая команда text/task/heading: проверка snapshot identity, revision, digest,
  новый разобранный снимок, no-op и overflow.
- Сохранить предыдущие проверки; raw byte helpers убрать из публичного API.

Не включены list mutations, table mutations, полный source-map всех контейнеров,
UI и production-интеграция.

## Ограничения и предположения

- Не выдавать descendant envelopes за полный диапазон контейнера.
- Не угадывать положение по одинаковому тексту; неподдержанные prefix/lazy/
  multiline/reference-context формы оставлять без структурной операции.
- Source bytes вне минимального проверенного диапазона не меняются.
- Ожидаемая структура проверяется повторным parse всего результата.

## Критерии приёмки

1. Все публичные edit-команды требуют один snapshot contract.
2. Другой документ с теми же bytes/revision, stale revision, ABA, invalid node,
   overflow и повреждённый token дают явный отказ без результата.
3. Есть тесты вложенных цитат, sibling preservation, CRLF/UTF-8/EOF, no-op,
   ancestor relationships/envelopes и неподдержанных случаев.
4. Windows/Linux Debug и Linux ASan/UBSan проходят.

## План проверок

Смешанные text → task → heading команды на последовательных снимках; identity
при независимом parse, копия того же снимка, duplicate text, quote depth 1–3,
отказ lazy/multiline/list forms, точное сохранение prefix/suffix.

## Зависимости и открытые вопросы

- Зависимость: Task 012.
- Полные list/quote ownership ranges и multiline mutations — Task 014.

## Исполнительский промпт

Реализуй Task 013 в experiments/md4c по этой карточке и Task 012. Объедини
редактирование под одним versioned API; отдели identity документа от hash bytes,
сохраняй identity при успешной команде, повышай revision только при изменении.
Добавь parent links, явно именованные evidence envelopes и bounded nested quote
mapping. Структурная операция не должна пересериализовать quote prefixes/соседей.
Там, где диапазон не доказан, верни явную ошибку. Обнови тесты старого API на новый,
проверь Windows/Linux и sanitizers. Итог: изменения, команды/результаты,
поддержанный scope и оставшиеся риски в этой карточке; не объявляй эксперимент
готовым production pipeline. Fixtures с недоверенным HTML не исполнять.

## Итог выполнения

### Реализация

- В `experiments/md4c/source_document.{h,cc}` добавлены parent links для всех
  узлов и `evidenceEnvelopes` с известными собственными/дочерними source bytes.
  Envelope не обещает наличие всей разметки и не используется как ownership range.
- `mapBlocks` поддерживает однострочные paragraph/непустые ATX в цепочке явно
  маркированных цитат. Prefix разбирается по числу quote ancestors, кандидат
  подтверждается isolated parse; без поиска совпадающего текста.
- Все три операции передаются через `EditCommand` в `applyEdit`.
  Старые byte helpers стали внутренними. Новый parse получает UUID identity;
  изменённый снимок сохраняет identity и получает revision + 1.
- Token проверяет UUID/revision/SHA256 до любой команды, включая no-op.
  No-op сохраняет bytes/revision/identity и допускается при максимальной revision;
  фактическая правка при исчерпанной revision отклоняется.
- Полный повторный parse проверяет ожидаемое дерево. Любая ошибка возвращает
  `ParseResult` без нового документа с явной диагностикой.
- README, ARCHITECTURE и Task 003 согласованы с новым экспериментальным контрактом.

### Проверки

На Windows UCRT64 GCC/Qt и Ubuntu WSL GCC/Qt:

```text
cmake -S . -B build-md4c-contract -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMARKPAD_BUILD_MD4C_SPIKE=ON
cmake --build build-md4c-contract --parallel
ctest --test-dir build-md4c-contract --output-on-failure
```

Windows: UCRT64 bin в PATH, configure дополнен
`-DCMAKE_PREFIX_PATH=C:\msys64\ucrt64`.
Ubuntu: исходник `/mnt/c/projects/git/markpad`, build `/tmp/markpad-md4c-contract`.
**Обе ОС: CTest 2/2; MD4C Qt Test 51 passed, 0 failed.**

Linux sanitizers:

```text
cmake -S /mnt/c/projects/git/markpad -B /tmp/markpad-md4c-contract-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMARKPAD_BUILD_MD4C_SPIKE=ON -DCMAKE_C_FLAGS=-fsanitize=address,undefined -DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined
cmake --build /tmp/markpad-md4c-contract-asan --target markpad_md4c_spike --parallel
env ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir /tmp/markpad-md4c-contract-asan -L md4c --output-on-failure
```

**ASan/UBSan/leak detection: 1/1 passed.** `clang-format` 23.1.0 Google
`--dry-run --Werror` и `git diff --check` — успешно.
CI использует тот же MD4C target; удалённый workflow для этих изменений не запускался.

Добавлены quote depth 1–3, compact/indented prefixes, CRLF/EOF/UTF-8+emoji,
повторные sibling blocks, ancestor/envelope checks, source preservation.
Для каждой команды проверены foreign identity при одинаковых bytes/revision,
повреждённый digest/identity, stale revision, no-op, invalid node, overflow и ABA.
Сохранены прежние fixtures через test adapters к публичному versioned API.
При проверках исправлены Windows-неоднозначность присваивания пустого QUuid
и неверные тестовые ожидания: общий envelope включает и предыдущий HTML,
повторное включение checkbox нормализует изменённый marker в lowercase `x`.

### Границы и следующие шаги

- Точные диапазоны остаются частичными: multiline/lazy quotes, list-contained
  blocks, Setext и reference-context forms не получили структурных операций.
- Публичный Document предполагается результатом parser; UUID не обеспечивает
  авторизацию или защиту от произвольной ручной мутации structs.
- Индексы узлов относятся к конкретному снимку; после команды их надо получать
  из нового дерева. DocumentSession, open/undo identity policy и UTF-16 mapping
  остаются за Task 003.
- Есть полный parse проверки результата и повторный parse возвращаемого снимка;
  производительность не измерена, incremental parsing не заявлен.
- Полные ownership ranges и список/цитата mutations — [Task 014](task_014.md);
  затем table mutations — [Task 015](task_015.md). Они пока не реализованы.
- Изменения локальные в `feature/task_002`; commit/push не выполнялись.
