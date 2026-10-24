# Task 001 — Базовый проект, сборка и CI

## Статус и владелец

- Статус: `In Progress`
- Владелец: исполнитель C++/CI; архитектор принимает контракты
- Приоритет: P0

## Контекст

Markpad — Qt 6 / C++20 desktop-приложение для Windows и Linux. Основной локальный Windows-профиль — MSYS2 UCRT64 + GCC; сборка через CMake + Ninja. Тесты запускаются через CTest; для тестов выбран Qt Test. Целевой CI — GitHub Actions на Windows/UCRT64 и Ubuntu/GCC.

## Цель

Создать минимальную, воспроизводимую основу приложения и сборки на обеих ОС без реализации пользовательских функций.

## Область работ

**Включено:**

- Минимальный каркас Qt Widgets/C++20 и `CMakeLists.txt`.
- CMake targets приложения и одного smoke-test target с Qt Test/CTest.
- Профили сборки Windows MSYS2 UCRT64/GCC и Linux GCC.
- Базовый workflow GitHub Actions для конфигурирования, сборки и запуска тестов на Windows и Ubuntu.
- Краткие проверенные команды локальной сборки в AGENTS.md.

**Не включено:** выбор GFM/PDF библиотек, реализация редактора, UI-дизайн сверх пустого запускаемого окна, установка пакетов на пользовательскую систему.

## Ограничения и предположения

- Использовать Qt 6 Widgets и Qt Test; не смешивать UCRT64 с MSYS/CLANG64 библиотеками.
- Сборка приложения — C++20, генератор Ninja.
- Не фиксировать версии пакетов без необходимости; в CI обеспечить повторяемую установку и указать используемые версии/источники.
- Не добавлять сторонние зависимости сверх минимально необходимых.

## Критерии приёмки

1. Репозиторий конфигурируется и собирается CMake/Ninja на Windows UCRT64 и Ubuntu/GCC.
2. Приложение запускается и создаёт одно пустое Qt окно.
3. Простой Qt Test проходит локально через CTest и в обеих CI job.
4. AGENTS.md содержит команды, которые реально соответствуют добавленной конфигурации.
5. Результат не включает неиспользуемые зависимости и не меняет продуктовые требования.

## План проверок

- Выполнить configure/build/ctest локально в UCRT64.
- Проверить обе CI job или, если запуск CI недоступен, сообщить это явно и проверить Linux локально/в контейнере при доступности.
- Проверить чистый configure из нового build-каталога.

## Зависимости и открытые вопросы

- Зависимости: нет.
- Открытые вопросы: нет для минимального каркаса.

## Исполнительский промпт

Ты — C++/Qt разработчик, выполняющий Task 001 Markpad. Прочитай README.md, AGENTS.md, ARCHITECTURE.md и эту карточку. Создай минимальный C++20/Qt 6 Widgets проект с CMake/Ninja и Qt Test/CTest, который собирается на Windows в MSYS2 UCRT64 с GCC и на Ubuntu с GCC. Добавь GitHub Actions matrix/jobs для этих двух профилей, smoke-test и фактические локальные команды в AGENTS.md. Не реализуй функции редактора и не выбирай Markdown/PDF зависимости. Не устанавливай пакеты на пользовательскую систему. Проверь сборку и тесты доступными средствами; если платформа недоступна, укажи это, не объявляя проверку пройденной. В итоговом сообщении укажи изменённые файлы, выполненные команды/результаты, недоступные проверки и отклонения от критериев.

## Итог выполнения

- Реализованы минимальное Qt Widgets приложение с одним пустым окном, общий factory окна, Qt Test smoke test/CTest и GitHub Actions jobs для Ubuntu/GCC и Windows MSYS2 UCRT64.
- Локально подтверждены версии GCC 16.2.0, CMake 4.4.3, Ninja 1.13.2 и установленный пакет Qt 6.11.2.
- Чистая Windows Debug-конфигурация прошла: `cmake -S . -B build-task001-verify -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:\msys64\ucrt64`.
- Windows сборка прошла: `cmake --build build-task001-verify --parallel`.
- Windows CTest прошёл: `ctest --test-dir build-task001-verify --output-on-failure` — 1/1 passed. Smoke test создаёт и показывает окно с offscreen platform plugin.
- Чистая Ubuntu Debug-конфигурация в WSL прошла: `cmake -S /mnt/c/projects/git/markpad -B /tmp/markpad-task001-verify -G Ninja -DCMAKE_BUILD_TYPE=Debug`.
- Ubuntu сборка прошла: `cmake --build /tmp/markpad-task001-verify --parallel`.
- Ubuntu CTest прошёл: `ctest --test-dir /tmp/markpad-task001-verify --output-on-failure` — 1/1 passed.
- GitHub Actions workflow содержит отдельные Ubuntu/GCC и Windows/MSYS2 UCRT64 jobs. CI ещё не запускался; отправка ветки должна запустить workflow, результат требуется проверить перед закрытием задачи.
- Локальные критерии обеих платформ подтверждены. Задача остаётся In Progress до успешного запуска обеих GitHub Actions jobs.

## Примечания завершения

- Проверки выполнены в чистых build-каталогах; временные каталоги проверки удалены.
- Повторный запуск Windows CTest без UCRT64 `bin` в `PATH` ранее завершался с `0xc0000139` из-за отсутствующих Qt runtime DLL. Команды в AGENTS.md устанавливают нужный runtime path.
- Workflow YAML ранее проверен установленным PyYAML.
