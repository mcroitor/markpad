# Task 024 — Table commands во вложенных контейнерах

## Постановка архитектора

- Статус: `Done`. Родитель: [015](task_015.md).
- Зависимости: [019](task_019.md), [023](task_023.md).
- Цель: nested mapping и применение шести table commands из 022/023.
- Scope: explicit QUOTE, LI с таблицей первым или последующим block,
  цепочка QUOTE → LI, depth 1–3; обязательные исключения RemoveTable ниже.
- Контракт: [015 v2](task_015.md) + [025 v1](task_025.md);
  код — `src/markdown_document.{h,cc}` и existing MD4C Qt Test.

## Обязательные проверки

| Source / команда | Ожидаемый исход |
|---|---|
| `"> | h |\n> | --- |\n"` / insert row a | Success: `"> | h |\n> | --- |\n> | a |\n"` |
| `"- | h |\n  | --- |\n"` / insert row a | Success: `"- | h |\n  | --- |\n  | a |\n"`; list marker сохранён |
| `"- p\n\n  | h |\n  | --- |\n"` / RemoveTable | Success: `"- p\n\n"`; LI остаётся, list tight (единственный LI с одним P) |
| `"> p\n>\n> | h |\n> | --- |\n"` / RemoveTable | Success: `"> p\n>\n"`; parent quote остаётся |
| Table первым block LI на строке `- ` / RemoveTable | Unsupported, даже если после table есть paragraph |
| Table единственным block LI или QUOTE / RemoveTable | Unsupported, нельзя каскадно удалять parent |
| `"> - p\n>\n>   | h |\n>   | --- |\n"` / insert row a | Success: parent quote/LI сохранены, row имеет prefix `>   ` |
| Structural TAB/lazy continuation внутри patch | Unsupported |

Каждую из шести команд проверить в quote и в последующем block LI; первые пять
также при header на строке list marker. Для глубины 3 — хотя бы row mutation.
Alignment/inline raw fragments, CRLF/EOF/UTF-8, foreign/stale/ABA/overflow обязательны.
В mapping утверждать per-line parentPrefix/tableFragment; внешний marker нельзя
считать owned table bytes. Reference fixtures — принятая policy 025, не новая.

### Reference fixtures 025 v1

- Source `"[new]: /new \"N\"\n\n> | h |\n> | --- |\n> | [c][new] |\n"` /
  insert row `z` в конец: success, source плюс `"> | z |\n"`, c → /new, N.
- То же с table subsequent block LI:
  `"[new]: /new \"N\"\n\n- p\n\n  | h |\n  | --- |\n  | [c][new] |\n"`;
  insert row z добавляет `"  | z |\n"`; prefix/definition прежние.
- Set alignment/insert column сохраняют c binding; remove row/column с c
  удаляют только этот use, не external definition. RemoveTable последующего
  LI block даёт `"[new]: /new \"N\"\n\n- p\n\n"`; definition сохраняется,
  даже если больше не используется. Прежние RemoveTable refusals остаются.
- Full/collapsed/shortcut/image references обязательны с CRLF/EOF/Unicode.
  Новые payload cells plain text: `"[x][new]"` не создаёт reference use.

## Исполнительский промпт v2

Расширь команды 022/023 на nested формы этой матрицы. Сохраняй parent marker,
depth/indentation и raw cells; не пересериализуй внешний list для table edit.
Различай обязательный success и фиксированный unsupported для RemoveTable.
Ожидаемые source/tree/prefix metadata задай до output parse. Используй
policy 025 v1 и existing definition inventory из 017/019, сохраняя winning
definition identity всех surviving uses; isolated parse не определяет context.
Используй
[общий порядок](README.md#исполнение-дочерних-md4c-задач); fixtures не исполнять.
Не объявляй общий nested GFM lossless, если прошли только перечисленные формы.

## Итог выполнения

### Реализация

- `mapNestedTable` распространяет mapping 021 на таблицы внутри LI/QUOTE через
  VirtualContent/parseVirtual 019: virtual table range/cells переводятся в real
  offsets, parent line prefixes сохраняются отдельно.
- `TableSource.continuationPrefix`/`blankLinePrefix` и serializer 022 добавлены
  во все шесть table commands. First table row остаётся на строке parent
  marker/continuation, последующие row lines получают parent prefix.
- Insert/remove row, insert/remove column, SetTableAlignment работают в quote,
  в LI с таблицей первым block на marker line и в LI с последующим block,
  включая depth 3. Expected tree строится из проверенного нового source, raw
  cells/references сохраняются, external definitions не перемещаются.
- RemoveTable:
  - subsequent block LI/QUote success, patch начинается с line start и удаляет
    parent prefixes таблицы, parent/соседи остаются; tightness LI обновляется;
  - table на LI marker line — explicit Unsupported;
  - table единственным block LI/QUOTE — explicit cascade Unsupported;
  - top-level удаление и structural mismatch при слиянии соседних контейнеров
    работают как в 023.
- Reference fixtures: full/collapsed/shortcut/image uses, definition bytes и
  binding identity сохраняются; remove row/column удаляет только use, definition
  остаётся. CRLF/EOF/Unicode и depth 3 покрыты.

### Expected → actual

| Fixture | Expected | Actual |
|---|---|---|
| Quote table insert row | `> | h |\n> | --- |\n> | a |\n` | Pass |
| LI marker-line table insert row | `- | h |\n  | --- |\n  | a |\n` | Pass |
| Subsequent LI table RemoveTable | `- p\n\n`, LI/список сохранены, tight | Pass |
| Quote subsequent table RemoveTable | `> p\n>\n`, quote сохранён | Pass |
| Table first block LI marker line RemoveTable | Unsupported | Pass |
| Table only block LI/QUOTE RemoveTable | Cascade Unsupported | Pass |
| Depth-3 quote→LI→quote row insert | Row с prefix `>   > ` | Pass |
| Six commands в quote/subsequent LI | Prefixes/raw cells/alignment сохранены | Pass |
| Full/collapsed/shortcut/image refs | Binding identity сохранена | Pass |
| CRLF nested row insert | Exact CRLF | Pass |
| Structural TAB/lazy/non-table source | Explicit unsupported/invalid | Pass |

### Проверки

- Windows UCRT64 Debug: build, **CTest 3/3 passed**, MD4C suite **232 passed**,
  0 failed/skipped.
- Ubuntu WSL GCC 15.2 / Qt 6.10.2: clean configure/build, CTest **3/3 passed**.
- Ubuntu sanitized Debug: ASan/UBSan/leaks, CTest **3/3 passed**.
- `git diff --check --ignore-space-at-eol` прошёл; сборка без warnings.
  Google clang-format в среде отсутствует; dry-run не выполнялся.
- Native visual QA, clang-tidy и remote CI не запускались.

### Ограничения и следующий шаг

- Поддержаны перечисленные nested shapes depth 1–3 (LI/QUOTE); произвольные
  mixed containers вне virtual mapping и UI-интеграция не входят в этап.
- RemoveTable для первого block LI на marker line и для единственного block
  LI/QUOTE остаётся явным Unsupported по контракту, не каскадным удалением.
- Lazy/TAB/open-fence и ragged/неоднозначные source shapes отклоняются.
- Полный parse остаётся verification-компромиссом; performance не
  оптимизировалась.
- Коммит/push не создавались; Task 015 готов к финальной приёмке.
