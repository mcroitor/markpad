# Task 012 — MD4C block source-map и структурные операции

## Статус и владелец

- Статус: `Done`
- Владелец: архитектор + C++ разработчик
- Приоритет: P0

## Контекст

Task 002 доказала локальные text/task edits. MD4C не предоставляет общие block ranges.
Требуется следующий изолированный шаг, не production-интеграция.

## Цель

Доказать source-preserving структурную правку и защиту от устаревшего снимка.

## Область работ

- Частичная карта блоков: top-level однострочные абзацы и ATX headings,
  привязанные к проверенным исходным text callbacks; вложенные/неоднозначные
  блоки явно не поддерживаются.
- Операция преобразования абзаца в heading, изменения heading level и обратно.
- Сохранение inline-синтаксиса внутри блока и bytes вне заменяемого блока,
  включая CRLF и отсутствие финального newline.
- Проверка revision + source digest; новый снимок после успешного изменения.
- Qt Test и ASan/UBSan в существующем opt-in MD4C harness.

Не включены универсальный сериализатор, UI, session/file service, table/list/code
структурные операции и изменение MD4C upstream.

## Ограничения и предположения

- Не искать диапазоны по совпадению текста; не объявлять частичную карту полной.
- Если диапазон/результат нельзя подтвердить — явный отказ без нового снимка.
- Проверять полное дерево повторного parse относительно ожидаемой структуры.
- Существующие text/task эксперименты сохраняются; versioned contract в этой
  задаче вводится для новой структурной операции, не выдаётся за готовую session.

## Критерии приёмки

1. Тесты доказывают изменение структуры, а не только строки.
2. Prefix/suffix вне блока побайтно сохранены, inline content не потерян.
3. Stale revision, другой исходник, invalid level/index, unsupported block и
   опасный переход возвращают диагностику.
4. Windows/Linux Debug, Linux sanitizers проходят; scope/ограничения отражены.

## План проверок

ATX levels 1–6, paragraph promote/demote, closing hashes, indentation, duplicates,
UTF-8, CRLF, EOF, no-op, stale/foreign token, revision overflow, Setext/multiline/
quoted blocks rejection, соседний Setext delimiter и reference definitions.

## Зависимости и открытые вопросы

- Зависимость: Task 002.
- Полная карта остальных блоков остаётся отдельной работой.
- Revision управляется вызывающим кодом для исходного parse; новая операция
  увеличивает её только после фактического изменения.

## Исполнительский промпт

Цель: реализовать ограниченный block source-map и структурную операцию в
experiments/md4c, используя эту карточку, Task 002 и архитектуру как входы.
Не расширяй GFM и не подключай библиотеку к приложению. Диапазон разрешай только
после проверки исходных offsets и соответствия отдельно разобранного блока
его поддереву; неподдержанные формы возвращают явную ошибку. Для heading edits
сохраняй исходный inline-фрагмент, нормализуй только изменяемый блок, проверяй
revision/digest и полное дерево результата; no-op не меняет bytes/revision.
Добавь тесты из плана, запусти Windows/Linux и sanitizers. Итог запиши здесь:
изменения, команды/результаты, ограничения и оставшиеся работы. Не называй
частичный source-map полным решением; при блокерах верни доказательства.

## Итог выполнения

- Выполнено в ветке `feature/task_002`, в изолированном
  [MD4C-прототипе](../../experiments/md4c/source_document.h).
- `mapBlocks` возвращает частичную карту top-level однострочных абзацев и
  непустых ATX headings. Диапазон выводится из source-backed text callbacks и
  границ строки, затем отдельный parse проверяет совпадение поддерева. Поиск
  одинакового текста и угадывание диапазонов не используются.
- `BlockSource` разделяет диапазон заменяемого блока (вместе с окончанием строки)
  и исходный inline-фрагмент без ATX-маркеров. При изменении уровня только этот
  блок нормализуется; содержимое и исходник вне блока сохраняются.
- `setHeadingLevel(document, token, node, level)` поддерживает уровни 1–6 и
  уровень 0 для абзаца. Проверяет revision + SHA256 исходника, возвращает новый
  разобранный снимок с revision + 1. No-op сохраняет bytes/revision. Переполнение
  версии — диагностический отказ.
- Проверка всего дерева после замены защищает от контекстных изменений: например,
  demote перед `---` отклоняется, если вместо абзаца возник бы Setext heading.
- Тесты проверяют реальные node types/атрибуты, точный исходник, повторяющиеся
  блоки, UTF-8, CRLF/EOF, closing hashes, indentation, inline code/HTML,
  stale/foreign token, ABA (возврат к прежним bytes с новой revision), overflow,
  no-op, сохранение соседних reference definitions и отказы unsupported forms.

### Фактические проверки

- Windows/UCRT64 GCC 16.2.0, Qt 6.11.2, CMake 4.4.3, Ninja 1.13.2:
  чистая Debug-сборка, **2/2 CTest**; внутри MD4C **43/43 Qt Test результатов**
  (включая прежние сценарии и init/cleanup).
- Ubuntu/WSL GCC 15.2.0, Qt 6.10.2, CMake 4.2.3, Ninja 1.13.2:
  чистая Debug-сборка, **2/2 CTest**.
- Ubuntu ASan/UBSan с leak detection: **1/1 MD4C CTest**, без ошибок.
- clang-format 23.1.0 Google `--dry-run --Werror`, `git diff --check` проходят.
- Production targets не изменены. GitHub Actions для этих изменений ещё не
  запускался; существующий MD4C job включает новые тесты автоматически.

Windows PowerShell:

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:PATH"
cmake -S . -B build-md4c-blocks -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:\msys64\ucrt64 -DMARKPAD_BUILD_MD4C_SPIKE=ON
cmake --build build-md4c-blocks --parallel
ctest --test-dir build-md4c-blocks --output-on-failure
```

Ubuntu из корня репозитория:

```sh
cmake -S . -B /tmp/markpad-md4c-blocks -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMARKPAD_BUILD_MD4C_SPIKE=ON
cmake --build /tmp/markpad-md4c-blocks --parallel
ctest --test-dir /tmp/markpad-md4c-blocks --output-on-failure
cmake -S . -B /tmp/markpad-md4c-blocks-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMARKPAD_BUILD_MD4C_SPIKE=ON -DCMAKE_C_FLAGS=-fsanitize=address,undefined -DCMAKE_CXX_FLAGS=-fsanitize=address,undefined
cmake --build /tmp/markpad-md4c-blocks-asan --target markpad_md4c_spike --parallel
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir /tmp/markpad-md4c-blocks-asan -L md4c --output-on-failure
```

### Границы контракта

- Снимки в эксперименте должны приходить из `parse`, без ручного изменения
  публичных полей/индексов дерева. Это не защищённый production API.
- Token проверяет revision и содержимое, но не уникальную identity документа:
  для разных документов с одинаковыми bytes/revision он одинаков. DocumentSession
  должна дополнительно связывать команды с конкретной сессией.
- Вызывающий код управляет версией при загрузке/замене исходника; после успешной
  структурной правки использует возвращённый снимок и его новые индексы.
  Старые text/task функции из spike не стали автоматически versioned-командами.
- Карта не поддерживает Setext, multiline, пустые headings, вложенные blocks,
  lists/tables/code и блоки с внешним reference context, который невозможно
  подтвердить при изолированном parse. Они не теряются: их bytes остаются
  исходными, а структурная операция возвращает ошибку.
- Карта строится по запросу с повторными разборами; производительность больших
  документов и инкрементальный mapping не измерялись.
- Полный block serializer, создание/удаление/перемещение блоков, table/list
  mutations, session/undo и UTF-16 UI-mapping остаются дальнейшей работой.
  Завершена ограниченная постановка, не весь pipeline WYSIWYG.
