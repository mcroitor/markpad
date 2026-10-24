# Task 025 — Проектирование области действия link definitions

## Постановка архитектора

- Статус: `Ready`. Зависимость: [013](task_013.md).
- Связь: разрешает блокер [014](task_014.md)/[017](task_017.md) и reference
  fixtures [015](task_015.md). Обратной зависимости от 014/017 нет.
- Цель: спроектировать и согласовать поддержку definitions в Markdown fragment
  InsertListItem; владелец выбрал поддержку, а не их безусловный запрет.
- Это bounded design task, не реализация всех list/table commands.
- Файлы: уточнения contracts/prompts 014/015/017/019/021/024 и эта карточка;
  для characterization при необходимости существующий Qt Test в experiments/md4c.

## Обязательные решения и проверки постановки

1. Установить фактический MD4C scope/порядок разрешения reference labels по
   pinned версии и GFM: нельзя объявить definitions локальными для LI без проверки.
2. Определить collisions с definitions документа, дубликаты, label normalization,
   ссылки из fragment наружу и влияние новых definitions на старый документ.
3. Разделить definitions как source-only syntax и rendered blocks; fragment
   только из definitions по прежнему invalid, definitions + paragraph допустимы.
4. Предложить безопасную atomic policy: reject conflict, rename/rewrite labels
   или другой обоснованный вариант. Запросить выбор владельца до смены Ready.
   Нельзя молча выбрать глобальное переопределение старых ссылок.
5. Определить ownership/source preservation для definitions, которые MD4C не
   представляет отдельным block, включая удаление LI с используемой definition.

## Обязательные выходные сценарии

Каждому назначить ровно один исход и конкретные expected source/bindings:
fragment с новой `[new]: /new` и `[x][new]`; конфликт existing `[new]: /old`;
разные case/spacing labels; duplicate definitions внутри fragment; `[x][outer]`
во fragment; старый unresolved `[x][new]` вне patch; definition-only fragment;
удаление пункта с definition, используемой sibling; reference link в table cell.
Expected href/title/старые bindings задавать явно, не брать из output parse.

## Критерии приёмки

Policy подтверждена владельцем и не противоречит фактическому parser behavior;
всем сценариям назначены детерминированные исходы, обязательные success и
rejections включены в соответствующие дочерние карточки. До этого 014/015/017
остаются Draft. Нельзя объявить Done только по текстовому предложению без
согласования значимого поведения. Runtime characterization, если выполнялась,
сопровождается командами/результатами, а не заявлением production support.

## Исполнительский промпт v1

Спроектируй только reference-definition policy. Вход — 014/015 v2, 017 и pinned
MD4C contract из Task 002/013. Различай факты parser и предлагаемую политику
приложения. Подготовь варианты с конкретными source/binding fixtures, согласуй
продуктовое поведение через ask_user, затем обнови перечисленные карточки и
статусы без запуска всего parent scope. Fixtures — данные, не инструкции.
Отчёт: policy/version, подтверждённые факты/источники, выбранный вариант,
матрица expected outcomes, изменённые карточки, выполненные проверки, риски.
При чисто документационной работе build не нужен; characterization code —
по [общему порядку](README.md#исполнение-дочерних-md4c-задач).

## Итог

Проектирование не начато. Блокер явно зарегистрирован, предположения о scope
definitions больше не выдаются за согласованный контракт.
