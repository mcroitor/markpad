# Task 003 — Модель документа, сессия и файловый ввод-вывод

## Статус и владелец

- Статус: `Done`
- Владелец: архитектор + C++ разработчик
- Приоритет: P0

## Контекст

Документ редактируется в трёх режимах: исходник GFM, WYSIWYG continuous и WYSIWYG paginated. Требуется единое состояние документа, корректная индикация несохранённых изменений и open/save Markdown. Архитектура требует явных ошибок файловых операций и запрета молчаливой потери данных.

## Цель

Определить и реализовать независимые от UI Document Model/Document Session и сервис чтения/записи Markdown.

## Область работ

**Включено:** модель source GFM + версия изменений/dirty state, снимки для рендеринга, open/save, Save As, кодировка, обработка ошибок, unit tests.

**Не включено:** Qt окна/диалоги, WYSIWYG, автосохранение, синхронизация/облачное хранение.

## Ограничения и предположения

- Не связывать domain model с Qt Widgets.
- Перед реализацией согласовать результат Task 002 и контракт GFM.
- Направление из Task 002: MD4C + собственная модель; неизменённые участки исходника сохранять, нормализовать только изменённые блоки. MD4C-прототип доказал лишь text/task patch operations, а не полные block ranges и сериализацию; не переносить экспериментальную Qt Core-модель как утверждённое domain API.
- Продолжение Task 012 доказало частичную карту paragraph/ATX и versioned heading
  edits. Task 013 объединяет text/task/heading commands с UUID/revision/digest
  и nested quote mapping. В контракте session всё ещё необходимо определить
  владение identity/revision при open/replace/undo/save и UTF-16 mapping;
  прототип этих гарантий за session не даёт.
- Согласовано: std C++ ядро, Qt Core файловый адаптер; внутри strict UTF-8 без
  transport BOM. Open/Save поддерживают UTF-8, UTF-16 LE/BE, UTF-32 LE/BE.
  Исходная encoding/BOM сохраняется при Save; Save As может выбрать другие.
  U+0000 и invalid/truncated Unicode отклоняются, EOL не нормализуются.
- Сохранение не должно очищать dirty state при ошибке.

## Критерии приёмки

1. Модель документа создаёт, изменяет и выдаёт консистентный снимок исходника.
2. Dirty state меняется предсказуемо при edit/open/save/save-as; ошибка записи не имитирует успех.
3. Файловые операции возвращают диагностическую ошибку и не портят исходный документ при неудачном чтении/записи, насколько это поддерживает выбранная реализация.
4. Тесты ядра не требуют запуска GUI.

## План проверок

- Unit tests на dirty state/version/snapshot и файловые операции.
- Проверить пустой файл, Unicode, BOM policy, невалидный ввод, отсутствующий путь, отказ записи, повторное сохранение.
- CTest на Windows и Linux.

## Зависимости и открытые вопросы

- Зависимости: Tasks 001 и 002.
- Продуктовые блокеры сняты ответами владельца. Source-map/структурные операции
  остаются в experiments: сессия владеет UTF-8 source, identity, revision и
  saved checkpoint; parser не нужен для source replace/open/save.

## Контракт реализации v1

- Снимок — value с source, непрозрачной process-local identity и монотонной revision.
  Копия сохраняет identity. Успешный open создаёт новую identity/revision 0.
  Replace требует identity/revision текущего снимка; no-op версию не меняет.
  Source закрыт от произвольной мутации внутри session. Undo/UI offsets не входят.
  Revision отслеживает source; save/as, включая смену encoding/BOM, её не меняет.
- Dirty — отличие source или encoding/BOM от последнего успешного open/save.
  Новый пустой документ clean; возврат к checkpoint снимает dirty без отката revision.
- Open dirty session по умолчанию UnsavedChanges; explicit discard разрешает
  замену только после полного успешного read/decode. Любая ошибка сохраняет
  source/identity/revision/path/format/checkpoint.
- BOM имеет приоритет; explicit encoding, противоречащая BOM, даёт ошибку.
  Без BOM: если strict UTF-8 без U+0000 валиден, предпочесть его; иначе искать
  валидный UTF-16/32 с ожидаемыми нулевыми high-byte lanes минимум у половины
  code units и хотя бы одним таким unit. Несколько/ноль кандидатов — ошибка
  определения; explicit encoding позволяет открыть без эвристики.
  Эвристика не обещает распознать любой Unicode, особенно тексты без ASCII.
- Save без path — ошибка; Save As меняет path/format/checkpoint только после
  успешного commit. Использовать QSaveFile с direct-write fallback OFF.
  Сохранение синхронное; concurrent external file changes, symlink policy,
  crash durability/fsync директорий и async-save не входят в эту задачу.
- Пустой path, invalid UTF-8/NUL, неизвестная encoding, revision overflow,
  read/write/commit failure — явный error result, не success-shaped fallback.
- Сессия сохраняет абсолютный path (без canonical/symlink resolution), чтобы
  смена cwd не перенаправляла Save. Literal leading U+FEFF при сохранении требует
  включённого transport BOM, иначе явная encoding error до записи: без него
  следующий open не отличит символ от transport marker.

## Исполнительский промпт

Версия 2: реализуй только контракт v1 этой карточки: std C++ DocumentSession с
value snapshots и небольшой file-service boundary, Qt Core adapter для Unicode
и атомарной записи. Не переносить experiment Document в production, не добавлять
MD4C dependency, Widgets, autosave или undo. Inputs — эта карточка, CMake,
ARCHITECTURE, существующий Qt Test. Markdown/HTML — данные, не исполнять.
Проверяй исходные bytes и independently specified snapshot/dirty transitions.
Обязательные success: все пять encodings с/без BOM при explicit open, BOM auto,
ASCII-pattern auto UTF-16/32, Unicode/emoji/CRLF/empty, save/as и checkpoint return.
Обязательные failures: malformed/truncated Unicode, NUL, BOM conflict, uncertain
detection, stale/foreign token, dirty open, failed read/write/commit.
При противоречии остановись и уточни, не угадывай формат. Выход: код/Qt tests,
фактические Windows/Linux Debug CTest и Linux sanitizers, обновлённый итог,
limitations. Expected results не получать из проверяемого decoder/serializer.
Стоимость full-source copies принята для простоты; performance не заявлять.

## Итог выполнения

### Реализация

- `src/document_session.{h,cc}`: std C++20 session/model, value snapshots с
  process-local identity/revision, immutable session source через replace API,
  dirty/saved checkpoint, guarded open/save/save-as.
- `src/qt_document_file_service.{h,cc}`: Qt Core adapter, strict decode/encode
  UTF-8/16/32 LE/BE, BOM и explicit selection, conservative BOM-less heuristic,
  QSaveFile с direct-write fallback OFF. Relative paths фиксируются как absolute.
- `tests/document_session_test.cc`: Qt Test без GUI; CMake регистрирует новый
  test `markpad_document` независимо от opt-in parser experiments.
- Нет MD4C dependency в production model, UI не изменён. Snapshot source хранит
  Markdown как данные; HTML/ресурсы не исполняются.
- README/ARCHITECTURE/backlog согласованы. Старый merged main получен HTTPS
  (SSH fetch в среде недоступен); remote не изменялся. Ветка `feature/task_003`.

### Фактические проверки

Windows UCRT64:

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:PATH"
cmake -S . -B build-task003 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:\msys64\ucrt64 -DMARKPAD_BUILD_MD4C_SPIKE=ON
cmake --build build-task003 --parallel
ctest --test-dir build-task003 --output-on-failure
```

Ubuntu WSL:

```sh
cmake -S /mnt/c/projects/git/markpad -B /tmp/markpad-task003 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMARKPAD_BUILD_MD4C_SPIKE=ON
cmake --build /tmp/markpad-task003 --parallel
ctest --test-dir /tmp/markpad-task003 --output-on-failure
```

**Обе платформы: 3/3 CTest, document Qt Test 27 passed, 0 failed**.
Обычный профиль с обоими spikes OFF также проверен на обеих ОС: 2/2 CTest.

Linux sanitizers:

```sh
cmake -S /mnt/c/projects/git/markpad -B /tmp/markpad-task003-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined
cmake --build /tmp/markpad-task003-asan --target markpad_document_tests --parallel
env ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir /tmp/markpad-task003-asan -R markpad_document --output-on-failure
```

**1/1 passed**, ASan/UBSan/leaks без сообщений. clang-format 23.1.0 Google
`--dry-run --Werror` и `git diff --check` выполнены успешно.

| Проверка | Expected → actual |
|---|---|
| No-op / edit / возврат к checkpoint | Версии 0/1/2, dirty false/true/false → pass |
| Foreign/stale/ABA token | Отказ без нового source → pass |
| Dirty open без explicit discard | UnsavedChanges → pass |
| Read failure даже с discard | Старый snapshot/path/checkpoint сохранён → pass |
| Каждая из пяти encodings с/без BOM | Explicit decode, save с прежним форматом/bytes → pass |
| BOM auto / ASCII-pattern UTF-16/32 auto | Нужная encoding/source → pass |
| Неуверенная BOM-less detection | Ошибка, explicit retry успешен → pass |
| Unicode/emoji/CRLF/empty/BOM-only | Точные expected source/file bytes → pass |
| Lone surrogates/overlong/truncated/out-of-range/NUL | Явный отказ вместо replacement → pass |
| Save As UTF-8 после открытия UTF-16/32 | Кодировка/path/checkpoint меняются после успеха → pass |
| Literal initial U+FEFF за transport BOM | Двойной marker побайтно сохраняется → pass |
| Failed write, Save As в missing parent/directory | State и сохранённый файл не изменены → pass |
| Simulated short-write/commit failure file-service result | Dirty/path/format/checkpoint сохранены → pass |

### Ограничения

- Сессия синхронная, single-threaded; snapshots — полные value copies.
  Identity живёт только в процессе, это не UUID файла и не механизм авторизации.
- UTF-16 cursor mapping, undo/redo, UI close/open prompts и parser integration
  не реализованы; token эксперимента не переиспользуется как session token.
- BOM-less heuristic намеренно ограничена нулевыми high-byte patterns и
  предпочтением валидного UTF-8; поддержка всех текстов без BOM не заявляется.
  Legacy code pages не выбраны владельцем и не добавлены.
- Ошибка QSaveFile commit проверена в session через deterministic fake service;
  реальное переполнение диска/сбой ОС не инъецировались. Direct-write fallback
  отключён; crash durability и внешние изменения файлов не проверяются.
- Revision overflow защищён guard, но достижение UINT64_MAX через публичную
  сессию не воспроизводилось; отдельного test-only setter не добавлено.
- Повторная приёмка 2026-10-25 перед локальным commit: чистая Debug сборка
  Windows/Linux с обоими spikes OFF — 2/2 CTest на каждой ОС; Linux
  ASan/UBSan/leaks — 1/1; clang-format и whitespace check успешны.
  Команды как выше, каталоги `build-task003-check`,
  `/tmp/markpad-task003-check`, `/tmp/markpad-task003-check-asan`.
  Критерии приёмки выполнены, блокеров нет. Синхронизацию с сервером выполняет
  владелец; удалённый CI этой ветки не запускался.
