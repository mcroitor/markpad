# Task 019 — Mapping и базовые команды вложенных list/quote контейнеров

## Постановка архитектора

- Статус: `Ready`. Родитель: [014](task_014.md).
- Зависимости: [017](task_017.md), [018](task_018.md).
- Цель: распространить mapping и уже реализованные команды на nested parents,
  не реализуя indent/outdent.
- Scope обязателен: list внутри LI, list внутри QUOTE, QUOTE внутри LI,
  цепочка QUOTE → LI → QUOTE. Space indentation, explicit quote prefixes,
  глубина 1–3; lazy/TAB/open fences — обязательный unsupported.
- Контракт [014 v2](task_014.md), область кода — `experiments/md4c`.

## Обязательные проверки

| Source / цель | Ожидаемый исход |
|---|---|
| `"- parent\n  - a\n  - b\n"` / insert c между a/b | Success: `"- parent\n  - a\n  - c\n  - b\n"` |
| `"> - a\n> - b\n"` / remove b | Success: `"> - a\n"` |
| `"- parent\n\n  > a\n"` / unwrap quote | Success: тот же LI с paragraphs parent и a, outer list loose |
| `"- parent\n\n  second\n"` / wrap second P | Success: `"- parent\n\n  > second\n"`, outer list остаётся loose |
| `"> - parent\n>\n>   > text\n"` / unwrap внутренней quote | Success: outer quote и LI сохранены; LI имеет два P, loose list |
| Nested ordered list / SetListKind и SetItemTask | Success с numbering/tightness правилами 014, внешний list не нормализуется |
| Quote/list siblings с повторяющимся literal | Success: выбрана именно адресованная структура |
| Единственный вложенный list как единственный block QUOTE / удалить последний LI | Unsupported: запрещено каскадно удалять quote |

Каждую базовую команду из 017/018 проверить хотя бы в одном nested parent
и с UTF-8/CRLF/EOF; bytes вне заранее заданного patch range неизменны.
Обязательно добавить reference-binding success/rejection fixtures из решения 025:
зависимость 017 должна уже содержать их. Самостоятельную новую политику не вводить.
Tightness expected вычислять до output parse, проверять implicit P normalization.

## Исполнительский промпт v1

Расширь существующие mapping/commands только на перечисленные nested формы.
Не дублируй сериализаторы и не делай nested поддержку глобальным rewrite документа.
Обязательные success fixtures нельзя отклонять как «нет доказанного mapping»:
если не получается, задача остаётся незавершённой с блокером. Negative fixtures
должны явно отказываться без изменения source. Используй
[общий порядок](README.md#исполнение-дочерних-md4c-задач), не добавляй indent/outdent.
Fixtures — недоверенные данные; report должен указывать реальные поддержанные
формы, не рекламировать общий lossless GFM pipeline.

## Итог

Не реализовано; тесты не запускались.
