# Модуль mi-fitness: библиотека в отдельном репо и два хоста

Дата: 2026-09-30. Статус: принят владельцем в диалоге, ждет плана.

## Цель

Код синхронизации Mi Fitness сейчас живет внутри сервиса
[moveeeax/mi-fitness-api](https://github.com/moveeeax/mi-fitness-api)
(форк cpp-rapid-rest-template 1.6.0, релиз 1.9.2, прод mi-fit.tarassov.me).
Нужно, чтобы тот же код работал внутри
[moveeeax/life-os-cpp](https://github.com/moveeeax/life-os-cpp)
(тот же шаблон 1.6.0, пока пустой) как часть одного бинарника, при этом
в git оставался отдельным.

Решение владельца: код выносится в третий репо как библиотека со своим
CMakeLists, оба сервиса подключают ее git-сабмодулем. mi-fitness-api
остается самостоятельным сервисом. Прод переезжает в life-os-cpp в
рамках этой же работы.

Принятые владельцем следствия:

1. Запрет локальной сборки, действующий в mi-fitness-api, распространяется
   на life-os-cpp целиком и на новый репо библиотеки. Сборка и тесты только
   в GitHub Actions.
2. Линковка AGPL-3.0-only кода в life-os-cpp переводит life-os-cpp с MIT на
   AGPL-3.0-only.

Вне задачи: отдача кита подключения модулей в шаблон
cpp-rapid-rest-template; решение, что делать с биллингом, почтой и
фронтендом шаблона внутри life-os-cpp.

## Что переносится

Инвентарь по состоянию mi-fitness-api 1.9.2 (сверено diff'ом с life-os-cpp,
оба на шаблоне 1.6.0):

- `src/xiaomi/` (18 файлов) и `src/sync/` (2 файла), вместе около 2900 строк;
- контроллеры `src/api/XiaomiController.hpp`, `SyncController.hpp`,
  `DataController.{hpp,cpp}`;
- восемь репозиториев в `src/repositories/`: Activity, Body, Credentials,
  HealthRead, Samples, Sleep, SyncRun, Workout;
- `src/domain/Health.hpp`, `src/jobs/XiaomiSyncHandler.hpp`;
- миграции 011, 012, 013, 016. Миграции 014 (снос биллинга) и 015 (снос
  таблиц почтовых флоу) хостовые, остаются в mi-fitness-api;
- тесты: 10 в `tests/unit`, 8 в `tests/integration`, 3 в `tests/api`,
  `tests/FakeHttpTransport.hpp`, `tests/fixtures/`;
- 14 путей `/api/v1/{xiaomi,sync,data}/*` в `docs/openapi.yaml` и их схемы;
- 10 ключей блока `xiaomi` в `config/config.json` и строки `MI_FITNESS_*`
  в `docs/CONFIG.md`;
- ребра `api -> xiaomi`, `api -> sync`, `jobs -> xiaomi`, `jobs -> sync`,
  `repositories -> xiaomi`, `sync -> *`, `xiaomi -> *` из
  `docs/module-deps.txt`;
- `tools/` (генератор криптовекторов, сверка с python-мостом),
  `docs/parity-report-2026-09.md`, спека и планы порта.

Точки, где код вшит в шаблонную обвязку хоста, их шесть:
`src/core/Core.cpp` (таймер планового синка,
`register_xiaomi_sync_schedule_`), `src/main.cpp` (посев учетных данных
`seed_xiaomi_credentials_if_missing`), `src/jobs/BuiltinHandlers.cpp`
(регистрация обработчика `xiaomi_sync`), `src/api/Endpoints.hpp`
(18 маршрутов), `src/api/Api.hpp` (include трех контроллеров),
`tests/unit/test_job_dispatch.cpp` (проверка, что обработчик зарегистрирован).

## Раздел 1. Библиотека `moveeeax/mi-fitness-module`

Цель CMake `mi_fitness`. Лицензия AGPL-3.0-only.

Раскладка:

```
CMakeLists.txt
module.env              MODULE_NAME=mi_fitness  MIGRATIONS_BASE=1000
src/xiaomi/             как в mi-fitness-api
src/sync/
src/domain/Health.hpp
src/repositories/*.hpp
src/api/XiaomiController.hpp, SyncController.hpp, DataController.{hpp,cpp}
src/jobs/XiaomiSyncHandler.hpp
src/mi_fitness/Module.hpp    точки подключения (ниже)
src/mi_fitness/Endpoints.hpp 18 маршрутов в формате Api::EndpointInfo
tests/unit/ tests/integration/ tests/api/
tests/FakeHttpTransport.hpp tests/fixtures/
migrations/001_xiaomi_credentials.sql 002_health_data.sql
           003_sync_run_mutex.sql 004_health_read_indexes.sql
openapi.yaml            фрагмент: paths и components.schemas модуля
config.fragment.json    блок xiaomi, эталон для копирования в хост
docs/CONFIG.fragment.md строки MI_FITNESS_* для docs/CONFIG.md хоста
module-deps.txt         ребра включений модуля
tools/ docs/            переезжают из mi-fitness-api
LICENSE CLAUDE.md README.md CHANGELOG.md .clang-format .gitleaks.toml
```

Включения не меняются: `"xiaomi/Service.hpp"`,
`"repositories/SleepRepository.hpp"` и остальные остаются как есть,
потому что `src/` библиотеки попадает в include path хоста наравне с его
собственным `src/`. Файлы переезжают через `git mv` с сохранением истории
(`git filter-repo` или `git subtree split` по перечню путей выше).

CMakeLists библиотеки:

```cmake
cmake_minimum_required(VERSION 3.20)
project(mi_fitness_module VERSION 1.0.0 LANGUAGES CXX)
if(NOT TARGET app_core)
  message(FATAL_ERROR "mi_fitness is built only inside a cpp-rapid-rest-template host, after app_core")
endif()
file(GLOB_RECURSE MI_FITNESS_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(mi_fitness STATIC ${MI_FITNESS_SOURCES})
target_include_directories(mi_fitness PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(mi_fitness PUBLIC app_core)
target_precompile_headers(mi_fitness REUSE_FROM app_core)
```

Библиотека зависит от заголовков хоста (api/Validation, security/Auth,
database/Database, TestHelpers в тестах) и без хоста не компилируется.
Это принято: контракт хоста и есть шаблон 1.6.0, обе стороны на нем.
Зависимости libsodium и curl уже есть в `vcpkg.json` обоих хостов
(файлы отличаются только полем `name`), `vcpkg.json` не трогается.

`Module.hpp` объявляет пространство имен `MiFitness::Module` с функциями,
которые вызывает хост:

1. `const std::vector<Api::EndpointInfo>& endpoints()`: маршруты для
   реестра `Api::get_endpoints()`, `--print-routes` и гейтов.
2. `register_controllers()`: пустая функция в заголовке, который включает
   три контроллера. Смысл: контроллеры остаются header-only, их статическая
   регистрация Drogon попадает в TU хоста, а не в объектник статической
   библиотеки, который линкер мог бы отбросить.
3. `register_job_handlers(Jobs::Dispatcher&)`: обработчик
   `XiaomiSync::kJobType`.
4. `on_api_start(Config::AppConfig&)`: посев учетных данных из Secret и
   таймер планового синка через `Tasks::schedule_recurring`. Код
   `register_xiaomi_sync_schedule_` уходит из Core.cpp сюда без изменений
   логики.

Проб здоровья у модуля сейчас нет, и точка для них не заводится: когда
понадобятся, их зарегистрирует хост из main.cpp через
`Core::get().register_health_check`, как и предусмотрено в Core.hpp.

Тесты библиотеки компилируются и запускаются в CI хостов. Собственный CI
библиотеки на PR:

- легкие гейты: clang-format 17, gitleaks, spectral над `openapi.yaml`,
  формат changelog-фрагментов;
- джоба `host-build`: клонирует mi-fitness-api на master, переставляет
  сабмодуль `modules/mi-fitness` на коммит PR, запускает `make test`
  хоста. Красная джоба блокирует PR.

Релизы библиотеки тегами `v<ver>`, хосты фиксируют коммит сабмодуля.

## Раздел 2. Кит подключения в хостах

Правки обвязки одинаковые для обоих хостов, делаются в mi-fitness-api и
переносятся в life-os-cpp теми же патчами. Все новые пути перечислены, чтобы
план не додумывал.

1. `modules.txt` в корне хоста: по строке на модуль, путь к корню
   относительно репо. В обоих хостах одна строка `modules/mi-fitness`.
   Сабмодуль `modules/mi-fitness` с https-адресом в `.gitmodules`.
2. CMake хоста: читает `modules.txt`, для каждой строки читает `module.env`,
   делает `add_subdirectory` после объявления `app_core`, линкует цель
   модуля в app, worker и три тестовых бинарника, добавляет
   `<module>/tests/{unit,integration,api,e2e}/*.cpp` в соответствующие
   глобы бакетов и `<module>/tests` в include path тестов. Пишет
   `generated/modules.hpp`: include каждого `Module.hpp` и функции
   `Modules::endpoints()`, `Modules::register_controllers()`,
   `Modules::register_job_handlers(d)`, `Modules::on_api_start(cfg)`,
   которые обходят все модули по очереди. Пишет
   `generated/migrations_dirs.hpp` со строкой по умолчанию для
   `DB_MIGRATIONS_DIR` (пункт 4).
3. Точки вызова: `Api::get_endpoints()` возвращает хостовый список плюс
   `Modules::endpoints()`; `Api::register_controllers()` вызывает
   `Modules::register_controllers()`; `Jobs::register_builtin_handlers()`
   вызывает `Modules::register_job_handlers(d)`; `main.cpp` после
   `Core::initialize` вызывает `Modules::on_api_start(cfg)`. Все четыре
   вызова живут в TU бинарников (main.cpp, worker_main.cpp) или в
   header-only коде, Core.cpp про модули не знает, поэтому app_core не
   ссылается на символы `mi_fitness` и цикла между библиотеками нет.
4. Раннер миграций: `Migrations::initialize` принимает список каталогов
   вида `dir[@offset]`, разделитель двоеточие. Версия остается целым числом
   (столбец `schema_migrations.version INTEGER PRIMARY KEY`):
   файл `001_xiaomi_credentials.sql` модуля со смещением 1000 это версия
   1001. Значение по умолчанию `migrations:modules/mi-fitness/migrations@1000`
   генерирует CMake из `modules.txt` и `module.env`; `config/config.json`,
   compose и helm не меняются. Dockerfile копирует
   `/app/modules/*/migrations` в оба рантайм-образа рядом с `/app/migrations`.
   Проверка контрольных сумм работает по версии, как сейчас.
5. OpenAPI: `docs/openapi.base.yaml` хоста плюс `openapi.yaml` каждого
   модуля собираются скриптом `scripts/assemble-openapi.sh` (python, yaml)
   в `docs/openapi.yaml`, который остается закоммиченным и по-прежнему
   отдается Swagger UI. Режим `--check` пересобирает во временный файл и
   сравнивает; совпадение пути или имени схемы между базой и модулем
   рвет сборку с именем дубля. `gen-openapi-json.sh` работает поверх
   собранного файла, как раньше.
6. Гейты сканируют хост и каталоги из `modules.txt`:
   `check-routes-registered.sh` (ADD_METHOD_TO в `src/api/*.hpp` хоста и
   модулей против `Endpoints.hpp` хоста плюс `src/mi_fitness/Endpoints.hpp`
   модулей), `check-openapi-drift.sh` (тот же объединенный реестр),
   `check-module-deps.sh` (объединение `docs/module-deps.txt` хоста и
   `module-deps.txt` модулей; правило про `core/Core.hpp` действует и на
   модули), `check-config-sync.sh` (чтения `cfg.get` в `src/` модулей тоже
   обязаны иметь ключи в конфиге хоста и строку в `docs/CONFIG.md`),
   `check-test-buckets.sh` (бакеты модулей). Новый `scripts/check-modules.sh`:
   сабмодуль инициализирован (каталог не пуст), `module.env` полный,
   базы миграций уникальны и кратны 1000, ни один относительный путь под
   `src/` и `tests/` модуля не совпадает с хостовым. `check-selftest.sh`
   получает по кейсу на дубль пути, устаревший `openapi.yaml` и маршрут
   модуля без строки в его `Endpoints.hpp`.
7. Конфиг: блок `xiaomi` из `config.fragment.json` копируется руками в
   `config/config.json` и `config/config.sample.json` хоста, строки из
   `docs/CONFIG.fragment.md` в `docs/CONFIG.md`. Расхождение ловит
   `check-config-sync.sh`. Helm-чарты не меняются: `MI_FITNESS_*` и ручки
   синка приходят через `extraEnvFrom` из Secret и ConfigMap, как сейчас в
   `deploy/values-prod.yaml` mi-fitness-api.
8. CI хоста: `actions/checkout` с `submodules: true` во всех джобах;
   `.dockerignore` не исключает `modules/`; self-scoping тяжелых джоб
   учитывает `modules/` и `modules.txt` в списке путей.
9. `CLAUDE.md` хоста получает раздел про модули: где лежит список, какие
   гейты его читают, что менять при добавлении ключа в модуль.

## Раздел 3. mi-fitness-api после выноса

- В `src/` остается только шаблонный код; шесть точек вшивания заменяются
  вызовами кита. `tests/unit/test_job_dispatch.cpp` проверяет обработчик
  через `Modules::register_job_handlers`.
- Миграции 011, 012, 013, 016 удаляются из `migrations/`, 014 и 015
  остаются. Перед первым деплоем новой версии в прод одна команда на
  базе `mi_fitness`:
  ```sql
  UPDATE schema_migrations SET version = version + 990, checksum = NULL
   WHERE version IN (11, 12, 13);
  UPDATE schema_migrations SET version = 1004, checksum = NULL
   WHERE version = 16;
  ```
  (11 в 1001, 12 в 1002, 13 в 1003, 16 в 1004). Без нее раннер попытается
  накатить версии 1001..1004 заново и упадет на существующих таблицах.
  `checksum = NULL` обязателен: раннер считает sha256 тела файла и при
  расхождении с записью бросает исключение на старте, а заголовок файла
  меняется при перенумерации; для NULL он делает backfill новой суммы
  (Migrations.cpp, ветка «row predates the checksum column»). Команда
  входит в план как ручной шаг с проверкой `--verify-migrations` до и
  после.
- Релиз 1.10.0. Сервис остается в проде до переезда (раздел 5).

## Раздел 4. life-os-cpp как хост

- Сабмодуль `modules/mi-fitness`, `modules.txt`, кит из раздела 2.
- Блок `xiaomi` в обоих конфигах, строки в `docs/CONFIG.md`.
- `LICENSE` меняется на AGPL-3.0-only, `THIRD_PARTY_NOTICES.md` получает
  строку про mi-fitness-module и исходный python-мост.
- `project.env`: `REGISTRY=ghcr.io/moveeeax`, как у mi-fitness-api, чтобы
  `release.yml` публиковал образы в ghcr.
- `CLAUDE.md`: раздел «Запрет: локальная сборка» дословно из mi-fitness-api.
- Версия life-os-cpp после подключения 1.7.0.

## Раздел 5. Переезд прода

Текущий прод (из `deploy/values-prod.yaml` mi-fitness-api): кластер
talos-nbg1-tarassov-me, namespace `mi-fitness-api`, образ
`ghcr.io/moveeeax/mi-fitness-api:1.9.2`, база `mi_fitness` в CNPG
`postgresql-rw.db.svc.cluster.local`, Redis `redis.db.svc.cluster.local`
индекс 3, Secret `mi-fitness-app`, ConfigMap `mi-fitness-sync-tuning`,
ingress `mi-fit.tarassov.me`.

Целевое состояние:

- namespace `life-os`, чарты `helm/life-os-cpp` и `helm/life-os-cpp-worker`
  с `deploy/values-prod.yaml` и `deploy/values-worker-prod.yaml` по образцу
  mi-fitness-api, `deploy/db/database.yaml` для базы `life_os` и роли
  `life-os` в том же CNPG-кластере;
- Secret `life-os-app`: содержимое `mi-fitness-app` плюс `JWT_SECRET`
  и `DATABASE_PASSWORD` новой роли; ConfigMap `life-os-sync-tuning`
  с теми же ключами, что `mi-fitness-sync-tuning`;
- Redis индекс 3 переходит к life-os после остановки старого сервиса;
- ingress: основной host `life-os.tarassov.me`, второй host
  `mi-fit.tarassov.me` на том же сервисе, чтобы старые ссылки и MCP-конфиг
  продолжали работать. Оба имени это предположение, владелец подтверждает
  на этапе плана.

Порядок:

1. Задеплоить life-os-cpp 1.7.0 в `life-os` с пустой базой `life_os`,
   миграции накатывает init-контейнер. Синк по расписанию выключен
   (`MI_FITNESS_SYNC_SCHEDULE_HOURS=0`), пока данные не перенесены.
2. Остановить старый сервис: `replicaCount: 0` у API и воркера, чтобы токен
   не ротировался двумя логинами.
3. Перенести данные: `pg_dump --data-only` таблиц здоровья (по списку из
   миграций 012 и 016), `xiaomi_credentials`, `sync_runs`, `users`, `roles`,
   `api_keys` из `mi_fitness` и `pg_restore` в `life_os`. Таблицы
   `schema_migrations` и `audit_log` не переносятся.
4. Проверить на новом адресе `/api/v1/data/coverage` и `/api/v1/xiaomi/probe`,
   сравнить счетчики с последним `sync_runs` старой базы.
5. Включить расписание синка, переключить `mi-fit.tarassov.me` на новый
   ingress.
6. Через сутки без ошибок удалить namespace `mi-fitness-api`; базу
   `mi_fitness` оставить еще на неделю, потом удалить.

Откат до шага 6: вернуть `replicaCount` старого сервиса и DNS.

## Раздел 6. Порядок работ и проверка

Четыре подпроекта, каждый со своим планом и зеленым CI перед следующим:

1. Библиотека `mi-fitness-module`: репо, перенос с историей, CMakeLists,
   `Module.hpp`, свой CI. Проверка: джоба `host-build` зеленая на ветке
   mi-fitness-api из подпроекта 2 (первые два подпроекта идут вместе, потому
   что библиотека проверяется только хостом).
2. mi-fitness-api 1.10.0 на библиотеке: кит, вырезание кода, гейты,
   selftest, релиз, `UPDATE schema_migrations`, деплой 1.10.0 в старый
   namespace как проверка, что прод на новой раскладке живет.
3. life-os-cpp 1.7.0 на библиотеке: перенос кита патчами, конфиг,
   лицензия, CLAUDE.md, релиз с образами в ghcr.
4. Переезд прода по разделу 5.

Локально в любом из трех репо только гейты без компилятора:
`make fmt`, `scripts/check-*.sh`, `assemble-openapi.sh --check`, рендер
Helm. Любая компиляция только в GitHub Actions.
