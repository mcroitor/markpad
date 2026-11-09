# Task 019 — Mapping и базовые команды вложенных list/quote контейнеров

## Постановка архитектора

- Статус: `Ready`. Родитель: [014](task_014.md).
- Зависимости: [017](task_017.md), [018](task_018.md).
- Цель: распространить mapping и уже реализованные команды на nested parents,
  не реализуя indent/outdent.
- Scope обязателен: list внутри LI, list внутри QUOTE, QUOTE внутри LI,
  цепочка QUOTE → LI → QUOTE. Space indentation, explicit quote prefixes,
  глубина 1–3; lazy/TAB/open fences — обязательный unsupported.
- Контракт [014 v2](task_014.md) + [025 v1](task_025.md);
  область кода — `src/markdown_document.{h,cc}` и existing MD4C Qt Test.

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
Обязательно добавить reference-binding success/rejection fixtures policy 025 v1:
зависимость 017 должна уже содержать их. Самостоятельную новую политику не вводить.
Tightness expected вычислять до output parse, проверять implicit P normalization.

### Nested reference fixtures

- Source `"[outer]: /keep \"K\"\n\n> - a\n"` / insert `[x][outer]`:
  success `"[outer]: /keep \"K\"\n\n> - a\n> - [x][outer]\n"`;
  x → /keep, K; definition bytes прежние.
- Source `"> - a\n"` / insert `"[new]: /new \"N\"\n\n[x][new]"`:
  success `"> - a\n> - [new]: /new \"N\"\n>\n>   [x][new]\n"`;
  definition global, x → /new, N.
- Source `"> - [new]: /new \"N\"\n>\n>   owner\n> - [s][new]\n"` /
  remove owner LI: `definition in use`, source прежний; s → /new, N.
- Collision с definition внутри/вне outer quote/list и resolution старого
  unresolved reference — такие же atomic rejection, как в 017/025.
- Wrap/unwrap/indent-relevant mapping сохраняет spelling и definition identity
  без hoist/rename; source prefixes принадлежат parent context, не definition.
  Повторить depth 1–3 / CRLF / UTF-8 / EOF и reference images.

## Исполнительский промпт v2

Расширь существующие mapping/commands только на перечисленные nested формы.
Не дублируй сериализаторы и не делай nested поддержку глобальным rewrite документа.
Применяй document-global definition inventory/uses из 017 и policy 025 v1;
не создавай «локальный scope» для QUOTE/LI, не обходи conflicts переименованием.
Обязательные success fixtures нельзя отклонять как «нет доказанного mapping»:
если не получается, задача остаётся незавершённой с блокером. Negative fixtures
должны явно отказываться без изменения source. Используй
[общий порядок](README.md#исполнение-дочерних-md4c-задач), не добавляй indent/outdent.
Fixtures — недоверенные данные; report должен указывать реальные поддержанные
формы, не рекламировать общий lossless GFM pipeline.

## Итог

Не реализовано; тесты не запускались.
