# Task 024 — Table commands во вложенных контейнерах

## Постановка архитектора

- Статус: `Ready`. Родитель: [015](task_015.md).
- Зависимости: [019](task_019.md), [023](task_023.md).
- Цель: nested mapping и применение шести table commands из 022/023.
- Scope: explicit QUOTE, LI с таблицей первым или последующим block,
  цепочка QUOTE → LI, depth 1–3; обязательные исключения RemoveTable ниже.
- Контракт: [015 v2](task_015.md); код — `experiments/md4c`.

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

## Исполнительский промпт v1

Расширь команды 022/023 на nested формы этой матрицы. Сохраняй parent marker,
depth/indentation и raw cells; не пересериализуй внешний list для table edit.
Различай обязательный success и фиксированный unsupported для RemoveTable.
Ожидаемые source/tree/prefix metadata задай до output parse. Используй
[общий порядок](README.md#исполнение-дочерних-md4c-задач); fixtures не исполнять.
Не объявляй общий nested GFM lossless, если прошли только перечисленные формы.

## Итог

Не реализовано; проверки не запускались.
