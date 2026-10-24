# Task 002 — Spike GFM-парсера и round-trip

## Статус и владелец

- Статус: `Done`
- Владелец: C++ разработчик; архитектор утверждает выбор
- Приоритет: P0

## Контекст

Документный формат — GitHub Flavored Markdown (GFM), включая таблицы и strikethrough. WYSIWYG режимы должны редактировать документ и сериализовать его обратно без молчаливой потери содержимого. В MSYS2 найден пакет `cmark` для CommonMark, но это не подтверждает наличие GFM и round-trip. Надо исследовать пригодную библиотеку, а не заранее предполагать её свойства.

## Цель

Проверить техническую реализуемость GFM parse-edit-serialize на Windows/UCRT64 и Linux и вынести обоснованное решение о библиотеке/адаптере.

## Область работ

**Включено:**

- Сравнить поддерживаемые GFM библиотеки и способ получить редактируемое дерево с сериализацией обратно.
- Проверить доступность/сборку кандидата на UCRT64 и Linux.
- Сделать небольшой изолированный spike/прототип с тестовыми fixtures: headings, emphasis, strikethrough, tables, task lists, fenced code, raw HTML, неизвестный/неподдержанный фрагмент и синтаксически некорректный ввод.
- Проверить лицензии, активность сопровождения и риск потери исходной семантики/форматирования.
- Записать выводы и рекомендацию в `docs/tasks/task_002.md`; зависимости интегрировать только после утверждения архитектором.

**Не включено:** production редактор, включение библиотеки в основной продукт без review, HTML/PDF экспорт.

## Ограничения и предположения

- Не подменять GFM на CommonMark.
- Не считать AST парсера редактируемым сериализуемым document model без доказательства.
- Если round-trip требует собственного сериализатора/адаптера, оценить объём и тестируемость; не скрывать потери неизвестных узлов.
- Не скачивать и не собирать непроверенные бинарники; использовать проверяемые исходники/пакетные источники и фиксировать лицензии.

## Критерии приёмки

1. Отчёт сравнивает кандидатов по GFM coverage, редактируемости, round-trip, Windows/Linux, лицензии и сопровождению.
2. Выполнен небольшой прототип или причина невозможности прототипа конкретно задокументирована.
3. Для fixtures показаны parse-edit-serialize результаты и ограничения, особенно для raw HTML и неизвестных конструкций.
4. Рекомендация содержит решение: выбрать кандидата, продолжить исследование или предложить ограничение требований; выбор не интегрируется в приложение без явного архитектурного решения.

## План проверок

- Build/run прототипа на Windows UCRT64 и Linux с отмеченными версиями инструментов.
- Автоматические проверки fixtures на наличие ключевой GFM семантики после цикла преобразования.
- Ручная сверка лицензий по исходному LICENSE/SPDX и платформенной доступности.

## Зависимости и открытые вопросы

- Зависимости: Task 001 (базовый CMake harness).
- Решено владельцем после демонстрации spike: исследовать MD4C с собственной моделью/сериализатором; сохранять неизменённые участки исходника, нормализовать только изменённые блоки.
- MD4C-прототип проверен: локальные правки normal-text leaves и task-маркеров сохраняют остальной исходник побайтно; это не универсальный сериализатор.
- Открытые вопросы: полные диапазоны блоков, структурные правки и форматирование, versioned edit operations; production-интеграция пока не утверждена.

## Исполнительский промпт

Ты — C++ разработчик, которому поручено исследование для Task 002 Markpad. Прочитай карточку и архитектуру. Исследуй GFM parser/document model варианты и проведи ограниченный технический spike поверх CMake harness из Task 001. Проверяй источники и лицензии, не выводи GFM поддержку из названия Markdown-библиотеки. Воспроизведи parse-edit-serialize на перечисленных fixtures и зафиксируй конкретные изменения/потери, Windows UCRT64/Linux сборку, команды и результаты. Не добавляй библиотеку в production target без утверждения архитектора. Если надёжного round-trip решения нет, представь варианты с последствиями и запроси решение, не меняя формат продукта. Обнови секцию итогов этой карточки и сообщи изменённые файлы/проверки/рекомендацию.

### Продолжение: MD4C

Цель: проверить выбранное владельцем направление MD4C + собственная модель с сохранением неизменённого исходника. Входы: эта карточка, ARCHITECTURE.md и существующий CMake/Qt Test harness. Создай отдельный opt-in MD4C-прототип; прежний сравнительный стенд сохрани, но CI сфокусируй на MD4C. Используй официальный source archive с commit/hash, проверь MIT notices. Не подключай MD4C к production targets.

Построй собственное дерево из callbacks с копированием временных данных. Проверь GFM headings/emphasis/strikethrough, таблицы с alignment, nested/ordered task lists, links, code, raw HTML, неизвестный и некорректный ввод. Не используй расширения вне согласованного GFM автоматически из convenience dialect macro. Исследуй доступность source ranges: не угадывай диапазон по поиску одинакового текста. Для ограниченных локальных правок докажи побайтное сохранение prefix/suffix, повторно разбери результат и проверь ожидаемую структуру; неподдержанные/неоднозначные правки возвращают явную диагностику без изменённого исходника. Не представляй patch-прототип как универсальный Markdown-сериализатор.

Проверки: Windows/UCRT64 и Ubuntu/WSL Debug + CTest, Linux ASan/UBSan, UTF-8/CRLF/повторяющийся текст, отсутствие правок, ошибочные диапазоны/типы и отказ от структурно опасной правки. Выход: код/тесты, команды и результаты, подтверждённые границы API и оставшиеся риски в этой карточке. Отдельно укажи, что доказано и чего недостаточно для source-preserving production-модели; при блокере не расширяй продуктовые требования.

## Итог выполнения

### Текущий результат: MD4C

- Последующее расширение оформлено отдельно как [Task 012](task_012.md):
  частичный block source-map и versioned paragraph/heading edits; там приведены
  новые проверки. Результаты 31/31 ниже относятся к этапу Task 002.
- По решению владельца работа сфокусирована на **MD4C + собственной модели**. Прототип находится в [experiments/md4c/](../../experiments/md4c/), опция `MARKPAD_BUILD_MD4C_SPIKE=ON`; по умолчанию выключена. Targets приложения с MD4C не связаны.
- Создано собственное дерево block/span/text из callbacks; строки и атрибуты копируются, временные указатели MD4C не хранятся. Исходный `QByteArray` остаётся каноническим текстом. Это экспериментальная Qt Core-модель, а не финальная Qt-независимая domain model Task 003.
- Реализованы две ограниченные операции: замена source-mapped normal-text leaf с экранированием Markdown и переключение task checkbox. Применяется одна локальная замена, затем полный повторный parse и сравнение с ожидаемым деревом. Неожиданное изменение структуры возвращает диагностический отказ, а не изменённый документ.
- Проверено **31/31 Qt Test результатов** (с init/cleanup), Windows/UCRT64 и Ubuntu/WSL Debug: **2/2 CTest** с исходным smoke test. **ASan/UBSan + leak detection** на Ubuntu проходят без ошибок на MD4C-прототипе.
- CI переключён на MD4C; прежний `cmark-gfm`/Qt стенд сохранён как необязательный сравнительный эксперимент, но не скачивается при включении только MD4C. Удалённый запуск обновлённого workflow ещё не выполнялся.
- Вывод: MD4C подходит для продолжения разработки как parser выбранной модели. **Полный source-preserving сериализатор и все WYSIWYG-операции пока не доказаны**; ограничения и следующий шаг описаны в разделе «MD4C: подтверждённые границы» ниже.

### Первый сравнительный этап (история)

- Исследование выполнено в ветке `feature/task_002`. `Done` означает завершение spike и отчёта, а не утверждение production-зависимости.
- Создан изолированный Qt Test прототип [experiments/gfm/round_trip_test.cc](../../experiments/gfm/round_trip_test.cc), включаемый через `MARKPAD_BUILD_GFM_SPIKE=ON` (по умолчанию `OFF`). Основные targets приложения с Markdown-библиотекой не связаны.
- На первом этапе проверены `cmark-gfm` (изменяемый AST и встроенный Markdown renderer) и `QTextDocument` (изменение через `QTextCursor`, `setMarkdown`/`toMarkdown`). На том этапе MD4C сравнивался только по документации; теперь отдельный MD4C-прототип реализован, см. текущие результаты.
- Debug-сборки и CTest проходят на Windows/UCRT64 и Ubuntu/WSL: **2/2 CTest**, включая исходный smoke test; внутри spike **39 Qt Test результатов**, включая init/cleanup.
- Дополнительный ASan/UBSan прогон обнаружил утечку в setter выравнивания таблицы `cmark-gfm`. Это отрицательный результат исследования, не скрытый и не подавленный.
- Первичная рекомендация исполнителя: **продолжить исследование**, не интегрировать ни один из кандидатов как готовый lossless round-trip pipeline. Предлагался `cmark-gfm` с адаптером и исправлениями. После ознакомления с результатами владелец выбрал альтернативное направление **MD4C + собственная модель/сериализатор**, описанное в решении ниже.

### Сравнение кандидатов

| Кандидат | GFM и модель | Parse-edit-serialize | Windows / Linux | Лицензия и сопровождение | Вывод |
|---|---|---|---|---|---|
| `github/cmark-gfm` | CommonMark + явно включаемые `table`, `strikethrough`, `tasklist`, `autolink`, `tagfilter`; изменяемый AST, API атрибутов | Есть `cmark_render_commonmark`, вызывающий render callbacks GFM-расширений. Семантика основных fixtures сохраняется, но исходный синтаксис нормализуется; найдены ошибки task lists и setter таблицы | Реально собран из исходников и протестирован в обоих целевых профилях | Основная библиотека BSD-2-Clause, отдельные включённые части MIT; полный состав в `COPYING`. Последний опубликованный release — `0.29.0.gfm.13`, 2023-07-21; текущий проверенный commit — 2026-09-28 | Наиболее близок к требуемому API, но без исправлений/адаптера не готов |
| `mity/md4c` | C parser с GFM-флагами, включая таблицы, task lists, strikethrough и autolinks; push callbacks, а не готовый изменяемый AST | Публичный API не предоставляет Markdown-сериализатор. В продолжении spike построено своё дерево и проверены локальные source-preserving правки; универсальный round-trip не подтверждён | В продолжении spike реально собран и протестирован на Windows/UCRT64 и Ubuntu/WSL | MIT, проверен `LICENSE.md`; commit `f9e5eb232b95b737373ae706648cb75bd259ed1a`, 2026-10-19 | Выбран владельцем для дальнейшей разработки собственной модели |
| Qt `QTextDocument` | Редактируемая rich-text модель; `MarkdownDialectGitHub` документирует поддерживаемое подмножество GitHub dialect, а не полную fidelity исходника | Готовые `setMarkdown`/`toMarkdown`, но теряются HTML-теги/атрибуты и выравнивание таблиц; прочие различия зависят от профиля Qt | Реально протестирован на Qt 6.11.2/Windows и Qt 6.10.2/Ubuntu | Qt Gui: commercial или LGPL-3.0/GPL по условиям Qt и соответствующим notices; версия 6.11.2 проверена по исходнику. Наличие Qt в проекте не превращает его лицензию в MIT | Возможен как представление для UI, но не как единственный источник документа |

Источники проверялись 2026-10-24:

- [`cmark-gfm` README](https://github.com/github/cmark-gfm/blob/27d942c8b0a62d192f616e5bf3578f4b6a89e180/README.md), [COPYING](https://github.com/github/cmark-gfm/blob/27d942c8b0a62d192f616e5bf3578f4b6a89e180/COPYING), [release](https://github.com/github/cmark-gfm/releases/tag/0.29.0.gfm.13), [проверенный commit](https://github.com/github/cmark-gfm/commit/27d942c8b0a62d192f616e5bf3578f4b6a89e180).
- [MD4C README](https://github.com/mity/md4c/blob/f9e5eb232b95b737373ae706648cb75bd259ed1a/README.md), [публичный API](https://github.com/mity/md4c/blob/f9e5eb232b95b737373ae706648cb75bd259ed1a/src/md4c.h), [LICENSE.md](https://github.com/mity/md4c/blob/f9e5eb232b95b737373ae706648cb75bd259ed1a/LICENSE.md).
- [Qt QTextDocument API](https://doc.qt.io/qt-6/qtextdocument.html), [исходник и лицензионный заголовок Qt 6.11.2](https://github.com/qt/qtbase/blob/v6.11.2/src/gui/text/qtextdocument.cpp).

`cmark-gfm` загружается только для spike из официального source archive GitHub, не как сторонний бинарник. Зафиксированы commit `27d942c8b0a62d192f616e5bf3578f4b6a89e180` и SHA256 `f8710b90b90a503240c920c6922367e700bb9895bb02018d6448d2f812fc7968`. Старый release не выбран вместо текущих исправлений. Номер версии API по-прежнему `0.29.0.gfm.13`; одного этого номера недостаточно для воспроизводимости.

При распространении будущих binaries необходимо сохранить copyright/license notices из `COPYING`. Файл также отдельно описывает CC-BY-SA 4.0 для upstream spec fixtures; они в наши тесты не копировались. Решение о production-лицензировании/упаковке эта задача не принимает.

### Как устроена проверка

Для каждого из 16 встроенных fixtures:

1. Разобрать исходник со всеми пятью GFM-расширениями и проверить наличие ожидаемого типа узла.
2. Сериализовать без редактирования, разобрать повторно и сравнить структурную сигнатуру.
3. Изменить literal `before` на `after` через API AST (для HTML/code изменяется соответствующий literal).
4. Проверить изменённый AST и результат повторного parse после serialize относительно ожидаемого изменённого документа.
5. Вывести фактический Markdown и признак byte-for-byte совпадения неизменённого round-trip.

Сигнатура включает типы и порядок узлов, literal, уровень заголовка, тип/начало/tightness списка, code info, URL/title, table alignment/header и checked-state задач. Игнорируются только пустые текстовые узлы, создаваемые autolink parser; soft breaks и содержимое не выкидываются. Это проверка выбранных свойств, а не полная GFM conformance suite.

Отдельно проверены пустой документ и реальные структурные изменения: уровень заголовка, выравнивание колонок, отметка выполнения задачи.

`qtCharacterization` измеряет результат Qt на тех же входах и выводит структурные различия. Для произвольной версии Qt тест **не требует** совпадения всех сигнатур: это сравнительное исследование, не принятие Qt как lossless-модели. Потеря raw HTML проверяется явно. Загрузка ресурсов документа заблокирована; HTML/скрипты не исполняются, экспорт не реализован.

### Результаты fixtures для cmark-gfm

Входы полностью заданы в `cmarkRoundTrip_data`; ниже показаны изменения `before` → `after`. `\n` обозначает перевод строки.

| Fixture | Полученный результат / ограничение |
|---|---|
| `headings` | Setext `before\n======` превращается в `# after\n`; уровень 1 сохранён |
| `emphasis` | `_before_ and __strong__` → `*after* and **strong**`; emphasis/strong сохранены |
| `strikethrough` | `~~before~~` → `~~after~~`; узел strikethrough сохранён |
| `table` | Сохраняются 3 колонки, значения, strong и выравнивания left/center/right; delimiter row нормализуется до `:--`, `:-:`, `--:` |
| `table-code-pipe` | Code cell с escaped pipe `a\|b` остаётся одной ячейкой с тем же code literal |
| `tasks` | Маркеры `* [ ] before`, `* [x] done` → `- [ ] after`, `- [x] done`; checked-state сохранён |
| `fenced-code` | Fence из `~~~cpp` заменяется на backticks с info `cpp`; code literal становится `after\n` |
| `raw-html-block` | `<div data-kind="opaque">after</div>`; тег и атрибут сохранены как raw literal |
| `raw-html-inline` | `Keep <span data-kind="opaque">after</span>.`; оба raw HTML узла сохранены |
| `unknown` | `:::note\nafter\n:::\n` сохраняется как обычный текст с soft breaks, а не как поддержанная directive |
| `malformed` | Незакрытые emphasis/link остаются текстом, спецсимволы экранируются; фатальной синтаксической ошибки нет |
| `unclosed-fence` | Незакрытый code fence получает закрывающий fence; code content и info сохраняются |
| `links` | Reference link становится inline link; URL/title и image alt/URL/title сохранены, reference definition как исходный синтаксис не сохраняется |
| `autolinks` | `www.example.com` становится явной ссылкой, email — `<user@example.com>`; URL и текст сохранены |
| `nested` | **Ограничение:** quoted task разобран как обычный list item; вывод `>   - \[ \] ~~after~~ and **bold**`. Совпадение с исходным AST не означает правильную поддержку GFM task lists |
| `unicode` | UTF-8 кириллица и emoji сохранены; проверяются данные, а не отображение консольного шрифта |

В 6 из 16 fixtures неизменённая сериализация побайтно совпала со входом; в остальных 10 исходник нормализовался. Это не гарантия сохранения произвольного Markdown.

Дополнительные отрицательные проверки:

- `nestedTaskIsNotRecognized`: `> - [ ] task` не содержит `tasklist` в AST. Сохранение такого AST не доказывает сохранность GFM-семантики.
- `orderedTasksLoseListKind`: `3. [ ] before\n4. [x] done` при serialize становится `- [ ] before\n- [x] done`; ordered-list type и начальный номер теряются.
- ASan/LeakSanitizer: `structuralEdits` через `cmark_gfm_extensions_set_table_alignments` оставляет **2 байта в 1 allocation**. Просмотр `extensions/table.c` подтверждает: setter создаёт новый alignment buffer и заменяет старый без освобождения. Библиотека не исправлялась и утечка не подавлялась; production-адаптер должен исключить/исправить этот путь.

Таким образом, зелёный обычный CTest подтверждает воспроизведение известных ограничений, а **не** полную пригодность библиотеки.

### Результаты Qt

- На обоих локальных профилях теряются HTML-теги и `data-kind`; например, `<div data-kind="opaque">before</div>` превращается в `after`.
- Выравнивание таблиц исчезает, а code cell с escaped pipe получает отличающееся экранирование и не совпадает с исходной структурой после повторного GFM parse.
- `:::note\nbefore\n:::` превращается в `:::note after :::`; текст остаётся, но переводы строк неизвестной конструкции потеряны.
- На проверенных профилях emphasis/strong также не проходит сравнение; Windows дополнительно отличается на strikethrough/nested fixture. Причина зависимости от Qt/шрифтов/окружения отдельно не изолирована.
- Autolinks нормализуются в ссылки с дополнительным soft wrap, поэтому строгая структурная сигнатура отличается; это не приравнивается к потере адреса ссылки.
- Headings, простые task lists, fenced code, незакрытый fence, malformed literal, links/images и UTF-8 прошли сравнение после правки на обеих ОС.

Сравнение использует parser `cmark-gfm` как измеритель структуры, а не независимый эталон GFM. Дефекты самого parser (например, quoted tasks) проверены отдельно.

### Воспроизведение и проверки

Windows PowerShell из корня репозитория:

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:PATH"
cmake -S . -B build-task002 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:\msys64\ucrt64 -DMARKPAD_BUILD_GFM_SPIKE=ON
cmake --build build-task002 --parallel
ctest --test-dir build-task002 --output-on-failure
ctest --test-dir build-task002 -L spike --verbose
```

Ubuntu из корня репозитория (в WSL можно использовать исходники из `/mnt/c/projects/git/markpad` и отдельный Linux build-каталог):

```sh
cmake -S . -B /tmp/markpad-task002 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMARKPAD_BUILD_GFM_SPIKE=ON
cmake --build /tmp/markpad-task002 --parallel
ctest --test-dir /tmp/markpad-task002 --output-on-failure
ctest --test-dir /tmp/markpad-task002 -L spike --verbose
```

Для надёжного сохранения подробного вывода Qt Test можно запустить `markpad_gfm_spike` из `build-task002/experiments/gfm` с `-o gfm-results.txt,txt` и `QT_QPA_PLATFORM=offscreen`. В Windows executable имеет расширение `.exe`; среда UCRT64 должна оставаться в `PATH`.

Профили фактических проверок:

| Профиль | Результат |
|---|---|
| Windows 11, UCRT64 GCC 16.2.0, Qt 6.11.2, CMake 4.4.3, Ninja 1.13.2 | Debug configure/build, CTest 2/2; spike 39/39 |
| Ubuntu/WSL, GCC 15.2.0, Qt 6.10.2, CMake 4.2.3, Ninja 1.13.2 | Debug configure/build, CTest 2/2; spike 39/39 |
| Spike `OFF`, обе платформы, отдельные чистые каталоги | Приложение/smoke test собираются, CTest 1/1; `_deps` не создаётся |
| Windows, spike `ON` и `BUILD_TESTING=OFF` | Конфигурация явно отклонена до скачивания зависимости |
| Ubuntu ASan + UBSan + leak detection | Полный spike завершается ошибкой из-за описанной утечки setter; отдельно воспроизведена на `structuralEdits` |
| Ubuntu ASan + UBSan без `structuralEdits`, leak detection включён | Остальные 38 Qt Test результатов проходят, санитайзеры ошибок не сообщили |
| Форматирование | Установленный clang-format 23.1.0, стиль Google, `--dry-run --Werror` |
| GitHub Actions | На первом этапе включался сравнительный spike; теперь оба job включают MD4C. Удалённый запуск для Task 002 ещё не выполнялся |

Воспроизведение sanitizer-проверки (полный прогон ожидаемо выявляет утечку):

```sh
cmake -S . -B /tmp/markpad-task002-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMARKPAD_BUILD_GFM_SPIKE=ON -DCMAKE_C_FLAGS=-fsanitize=address,undefined -DCMAKE_CXX_FLAGS=-fsanitize=address,undefined
cmake --build /tmp/markpad-task002-asan --target markpad_gfm_spike --parallel
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir /tmp/markpad-task002-asan -R markpad_gfm_round_trip --output-on-failure
```

Upstream CMake выдаёт deprecation warning о compatibility с CMake < 3.10; сборке это не мешает. Новые зависимости на пользовательскую систему не устанавливались.

### Первичная рекомендация до выбора владельцем (история)

1. Не использовать `QTextDocument::toMarkdown()` как каноническое сохранение всего документа.
2. Не считать `cmark-gfm` AST готовой продуктовой моделью. Использовать его как кандидата для дальнейшего исследования с обязательной проверкой nested/ordered task lists и исправлением утечки.
3. До интеграции определить контракт сохранения: каноническая нормализация всего документа либо сохранение исходных неизменённых участков с нормализацией только изменяемых блоков.
4. Raw HTML хранить как непрозрачные исходные фрагменты, а не преобразовывать обратно из rich text; активный контент при отображении не исполнять. Неизвестный синтаксис не объявлять поддержанным и не терять молча.
5. Собственный адаптер потребует mapping AST ↔ document model, типизированных edit operations, source ranges/исходных фрагментов, диагностики неподдержанных преобразований и тестов сериализации каждого типа. Нормализующий вариант проще, source-preserving вариант существенно дороже из-за привязки правок к исходнику, вложенности и конфликтов диапазонов. Оценка в днях без согласованного контракта не подтверждена.
6. Альтернатива — MD4C с собственной моделью/сериализатором: меньше зависимости от AST cmark, но больше собственного кода и отдельная проверка всех GFM-расширений. Подмена GFM на CommonMark и молчаливое отключение task lists не предлагаются.

### Решение владельца после spike

- Направление следующего исследования: **MD4C с собственной моделью документа и Markdown-сериализатором**, а не доработка `cmark-gfm`.
- Контракт сохранения: **неизменённые участки исходного Markdown сохраняются; нормализация разрешена только для изменённых блоков**. Глобальная пересериализация документа после локальной WYSIWYG-правки не соответствует этому решению.
- Следующий прототип должен доказать привязку callbacks/узлов к исходным диапазонам, сохранение opaque HTML/неизвестных фрагментов, nested/ordered task lists, table alignments и корректность правок без изменения соседнего исходника. Наличие таких возможностей у MD4C пока не заявляется как проверенный факт.
- Production-интеграция по-прежнему не утверждена. Текущий `cmark-gfm` experiment остаётся воспроизводимым сравнительным стендом; он не становится зависимостью приложения.
- Task 003 остаётся `Draft`: необходимо уточнить контракт модели на основе выбранного направления; реализация следующей задачи автоматически не начинается.

### MD4C: подтверждённые границы

**Источник и лицензия.** Официальный source archive [mity/md4c](https://github.com/mity/md4c/tree/f9e5eb232b95b737373ae706648cb75bd259ed1a), версия исходников 0.6.0, commit `f9e5eb232b95b737373ae706648cb75bd259ed1a`, SHA256 `61a3fe09f1c325d738777fb08cf959162bacda5888de0f7578e82a39df473780`. MIT проверена по загруженному `LICENSE.md`; при распространении необходимо включать copyright/license notice. Source archive скачивается CMake только при включённом эксперименте; библиотеки/утилиты в систему не устанавливаются, HTML renderer не собирается.

**Диалект.** Используются только `MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS | MD_FLAG_PERMISSIVEAUTOLINKS`. Macro `MD_DIALECT_GITHUB` намеренно не используется: в закреплённом upstream он дополнительно включает admonitions и footnotes. Underline, wiki links, math и другие несогласованные расширения не включаются.

**Source mapping.**

- `MD_BLOCK_LI_DETAIL::task_mark_offset` даёт документированный offset одного символа внутри `[ ]`/`[x]`; он проверяется перед правкой.
- У обычного block/span callback **нет общего start/end диапазона исходника**. MD4C не предоставляет готовую concrete syntax tree.
- Для text callbacks прототип проверяет, что buffer целиком находится внутри сохранённого исходника, и вычисляет byte offset. Если MD4C использует временный/синтезированный buffer, source range не назначается. Дополнительно перед правкой сверяются bytes/range с literal; совпадения текста поиском не используются.
- Это проверенное поведение закреплённой версии, а не обещание upstream возвращать source pointers во всех callbacks. Переход на другую версию требует повторения тестов. UTF-8 byte offset не равен позиции QTextCursor; UI-mapping пока не реализован.

**Модель и запись.** Снимок содержит исходник и дерево; локальная операция создаёт новый текст, не меняя исходный снимок. Без правок используется исходник, а не реконструкция Markdown из callbacks. Для изменённого text leaf пунктуация экранируется, но неизменённые prefix/suffix не сериализуются заново. После результата нужен новый parse: индексы узлов/диапазоны относятся к исходному снимку. Полный повторный parse используется как защитная проверка; инкрементальность и скорость на больших документах не исследовались.

| Проверка | Подтверждённый результат |
|---|---|
| Headings, emphasis/strong, strikethrough | Собственная модель различает типы/уровни; text edit сохраняет исходные маркеры вне изменяемого leaf |
| Таблицы, alignment, code с escaped pipe | Сохраняются исходные колонки/маркеры; распознаны left/center/right; текстовая правка одной ячейки не меняет delimiter row и соседние ячейки |
| Quoted/nested/ordered tasks | Распознаются как задачи; сохраняются начало ordered list, маркеры и вложенность. Checkbox правит только task-mark byte |
| Links/autolinks/images | Проверены URL/title, reference link остаётся reference link при правке отображаемого текста; адреса и definition не пересериализуются |
| Raw HTML | Теги/атрибуты сохраняются как исходные bytes; обычный текст между inline HTML тегами можно изменить, не меняя тегов. HTML не исполняется и не загружает ресурсы |
| Fenced code и незакрытый fence | Содержимое распознаётся, исходник сохраняется без добавления закрывающего fence. Редактирование code/HTML literal намеренно отклоняется в этом ограниченном spike |
| Неизвестный синтаксис и malformed Markdown | Неизвестные конструкции остаются текстом; окружающие исходные bytes сохраняются, синтаксическая нормализация всего документа не выполняется |
| Повторяющийся текст | Правится выбранный callback range, а не первое совпадение строки |
| UTF-8, emoji, CRLF | Проверены multibyte prefix/replacement и сохранение CRLF; невалидный/обрезанный UTF-8 отклоняется с диагностикой |
| Неподдержанные правки | Невалидный node/range, отсутствие mapping, entities, code/HTML, пустая/многострочная замена и NUL возвращают ошибку без нового исходника |
| Структурно опасная правка | Замена `_before_` на текст с пробелами у границ emphasis отвергается после повторного parse; success-shaped потери нет |
| Нет фактического изменения | Текст и уже установленный task-state сохраняют исходник, включая uppercase `[X]` |

Сравнение дерева включает порядок block/span/text, literal, heading level, list start/tightness, task-state, table dimensions/alignment, code info/lang и link/image attributes. Соседние text callbacks одного типа объединяются в сигнатуре: экранирование может дробить их без изменения содержания. Это всё ещё **не независимая GFM conformance suite** и не доказательство всех комбинаций синтаксиса.

**Границы на момент завершения Task 002:** полные block ranges,
создание/удаление/перенос блоков, изменение уровня заголовка/форматирования/table
structure с частичной сериализацией, edits через несколько callbacks/entities,
code/HTML editing, UTF-16 UI positions, undo/redo и контроль версии правки тогда
не были реализованы. Последующие Tasks 012/013 добавили ограниченные heading
operations и единый versioned edit contract; актуальное продолжение — ниже.
Полную source-map нельзя подменять regex-поиском или сериализацией всего документа.

**Запуск текущего MD4C-стенда** (Windows: предварительно установить UCRT64 `PATH` и сохранить `-DCMAKE_PREFIX_PATH=C:\msys64\ucrt64`):

```sh
cmake -S . -B build-md4c -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMARKPAD_BUILD_MD4C_SPIKE=ON
cmake --build build-md4c --parallel
ctest --test-dir build-md4c --output-on-failure
ctest --test-dir build-md4c -L md4c --verbose
```

Ubuntu ASan/UBSan:

```sh
cmake -S . -B /tmp/markpad-md4c-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMARKPAD_BUILD_MD4C_SPIKE=ON -DCMAKE_C_FLAGS=-fsanitize=address,undefined -DCMAKE_CXX_FLAGS=-fsanitize=address,undefined
cmake --build /tmp/markpad-md4c-asan --target markpad_md4c_spike --parallel
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir /tmp/markpad-md4c-asan -L md4c --output-on-failure
```

Фактически выполнено:

- Windows/UCRT64 и Ubuntu/WSL, версии инструментов/Qt как в таблице первого этапа: Debug configure/build, **2/2 CTest**, MD4C **31/31 Qt Test результатов**.
- Ubuntu ASan/UBSan + leak detection: **1/1 MD4C CTest**, без сообщений об ошибках.
- Windows чистая обычная сборка с обоими spikes `OFF`: **1/1 smoke test**, `_deps` не создаётся.
- Ubuntu с обоими spikes `ON`: **3/3 CTest**; сравнительный стенд не сломан.
- Windows MD4C `ON` + `BUILD_TESTING=OFF`: явная CMake-ошибка до скачивания зависимости.
- clang-format 23.1.0 Google `--dry-run --Werror`, `git diff --check`.
- GitHub Actions для этого изменения ещё не запускался.

**Следующий шаг:** продолжать MD4C-модель, сначала спроектировав block/source-map и versioned edit contract для Task 003/005. Не переносить экспериментальную модель в production без уточнения этих границ. Task 002 завершена как техническое исследование, а не как реализация всех операций редактора.

### Продолжение после исследования

- [Task 012](task_012.md) и [Task 013](task_013.md) завершены: частичные ranges
  однострочных P/ATX, в том числе во вложенных quotes, и общий versioned контракт
  text/task/heading с UUID/revision/digest. Их результаты не меняют исторические
  измерения/количество тестов Task 002.
- [Task 014](task_014.md) и [Task 015](task_015.md) — родительские контракты v2,
  Draft до согласования definitions/reference policy [025](task_025.md).
  Дочерние этапы 016–020 реализуют lists/quotes, 021–024 — tables.
  Исполнение начинается с [016](task_016.md), проектирование 025 независимо.
  Matrices теперь различают обязательные success/no-op/rejection; expected
  tree/ranges задаются независимо от проверяемого output.
- Контракты 014/015 документированы, но соответствующие команды и полные ownership
  ranges пока не реализованы. Production-интеграция автоматически не разрешена.
