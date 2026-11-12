# Task 047 — PDF export layout parity

## Статус и владелец

- Статус: `Blocked` (ждёт Task 044 и Task 046).
- Владелец: архитектор/координатор; `cpp-developer`, `test-writer`.
- Приоритет: P1 (release blocker по owner QA).
- Источник: [release checklist, PDF export](../release_checklist.md#pdf-export).

## Контекст

Пользователь сообщает, что exported PDF не совпадает с ожидаемой раскладкой:
размер страницы или размер текста не соответствует Paged WYSIWYG/эталонному
представлению. Сейчас PDF export использует Qt text layout, а paged status —
`PageLayoutEngine`; на QA fixture paged view показывает 3 страницы, а PDF — 2.
Требуется устранить расхождение или явно определить эквивалентность layout.

## Цель

Сделать PDF export визуально и структурно эквивалентным согласованной page
layout модели (page size, margins, typography, block order, page breaks) и
убрать неожиданное расхождение page count/масштаба.

## Область работ

**Включено:**

- Проверить PDF page size, margins, font family/size, line wrapping и block
  pagination на фиксированных fixtures.
- Согласовать PDF layout с Task 044 page-surface и Task 046 block styles.
- Исправить расхождения; если pixel-perfect Qt/PDF невозможен, зафиксировать
  точный structural contract и допустимые отличия.
- Добавить regression tests: page count, page size, отсутствие потери блоков,
  сравнение текста/порядка; visual fixture при возможности.
- Обновить README/Task 008 ограничения и release checklist.

**Не включено:**

- Новый PDF/HTML engine без архитектурного обоснования.
- Изменение GFM AST или пользовательского документа.
- Физическая печать/принтеры — Task 011 external checks.

## Ограничения и предположения

- PDF не обязан быть pixel-perfect относительно QTextEdit, если это явно
  задокументировано; текущее расхождение 3 vs 2 страницы считается дефектом до
  решения owner.
- PDF должен сохранять page size/margins/шрифты и порядок блоков.
- Исправления не должны менять source/token/history.

## Критерии приёмки

1. PDF page size/margins соответствуют PageSetup и paged mode.
2. Body/heading/code/quote typography соответствует WYSIWYG-настройкам (Task 043)
   и Task 046.
3. Page count и размещение блоков совпадают с paged layout для fixtures либо
   расхождение явно принято owner с точным описанием.
4. Нет потери/дублирования блоков, таблиц, code, ссылок и изображений;
   raw HTML остаётся безопасным.
5. Тесты покрывают page count/size, порядок блоков и typography; screenshots/PDF
   evidence обновлены.

## План проверок

- PDF export tests с фиксированными fixtures; `pdfinfo`/текстовая проверка.
- Сравнение page count paged mode и PDF.
- Windows Debug/Release и Ubuntu Debug/Release/ASan CTest.
- Visual review exported PDF и paged screenshots.

## Зависимости и открытые вопросы

- Зависимости: Task 044 (page surface), Task 046 (block presentation/typography).
- Нужно подтвердить owner: допустимо ли структурное, а не pixel-perfect
  совпадение, и какой эталон считать основным — Paged WYSIWYG или PDF.

## Исполнительский промпт

После завершения Task 044/046 сравни PDF с page-layout моделью на fixtures,
исправь page size/margins/typography/pagination и добавь regression tests.
Не объявляй PDF эквивалентным без проверки page count и порядка блоков; при
невозможности устранить расхождение остановись и верни точный структурный diff.

## Итог выполнения

Ожидает Task 044/046 и решения owner по эталону.
