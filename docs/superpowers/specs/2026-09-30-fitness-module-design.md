# Модуль fitness: перенос кода mi-fitness-api в life-os-cpp

Дата: 2026-09-30. Статус: принят владельцем в диалоге, ждет плана.

## Цель

Код синхронизации Mi Fitness живет в сервисе
[moveeeax/mi-fitness-api](https://github.com/moveeeax/mi-fitness-api)
(форк cpp-rapid-rest-template 1.6.0, релиз 1.9.2). Он переезжает в
[moveeeax/life-os-cpp](https://github.com/moveeeax/life-os-cpp) (тот же шаблон
1.6.0, пока без прикладного кода) как родной модуль `fitness` с маршрутами
под `/api/v1/fitness/*` и своей моделью доступа на битах прав шаблона.
API-ключи шаблона остаются рабочим способом машинного доступа.

Репо mi-fitness-api в этой задаче не меняется и продолжает работать в проде.

Вне задачи: переезд прода и данных из базы `mi_fitness` (отдельная спека
после зеленого CI), судьба репо mi-fitness-api после переезда, биллинг,
почта и фронтенд шаблона внутри life-os-cpp.

## Правила, действующие в life-os-cpp с этой задачи

Запрет локальной сборки, принятый в mi-fitness-api, распространяется на
life-os-cpp целиком: ни `make test`, ни `make test-unit`, ни разовая
компиляция файла. Сборка и тесты только в GitHub Actions. Локально
разрешено то, чему не нужен компилятор: `make fmt`, `scripts/check-*.sh`,
`make lint-openapi`, рендер Helm. Раздел «Запрет: локальная сборка»
переносится в CLAUDE.md life-os-cpp дословно из mi-fitness-api.

## Что переносится

Инвентарь по mi-fitness-api 1.9.2, сверено diff'ом двух деревьев:

- `src/xiaomi/` (18 файлов) и `src/sync/` (2 файла), около 2900 строк;
- контроллеры `XiaomiController.hpp`, `SyncController.hpp`,
  `DataController.{hpp,cpp}`, всего 655 строк; переносится логика, не файлы;
- восемь репозиториев: Activity, Body, Credentials, HealthRead, Samples,
  Sleep, SyncRun, Workout;
- `src/domain/Health.hpp`, `src/jobs/XiaomiSyncHandler.hpp`;
- миграции 011 (xiaomi_credentials), 012 (health_data), 013
  (sync_run_mutex), 016 (health_read_indexes). Миграции 014 и 015 сносят
  биллинг и почтовые таблицы, это решение того сервиса, в life-os-cpp они
  не переносятся;
- тесты: 10 файлов в `tests/unit`, 8 в `tests/integration`, 3 в `tests/api`,
  `tests/FakeHttpTransport.hpp`, `tests/fixtures/xiaomi_crypto_vectors.json`;
- 14 путей в `docs/openapi.yaml` и их схемы;
- 10 ключей блока `xiaomi` в конфиге, строки `MI_FITNESS_*` в `docs/CONFIG.md`;
- `tools/` (генератор криптовекторов, сверка с python-мостом),
  `docs/parity-report-2026-09.md`.

Точки, где код вшит в обвязку хоста, их шесть: `Core.cpp` (таймер
планового синка), `main.cpp` (посев учетных данных), `BuiltinHandlers.cpp`
(обработчик задачи), `Endpoints.hpp` (реестр маршрутов), `Api.hpp`
(include контроллеров), `tests/unit/test_job_dispatch.cpp`. В life-os-cpp
те же шесть точек заполняются заново под новые имена.

Переносится исходниками через `git mv` в копии дерева mi-fitness-api,
история коммитов при этом не сохраняется (два репо без общей истории,
файлы приходят одним коммитом). Заголовки файлов, комментарии и тесты
правятся под новые пути и имена.

## Раскладка в life-os-cpp

По ADR 0003 и гейту module-deps: домен, репозитории, контроллер и обработчик
задач в своих корзинах, все про облако Xiaomi и синк в одном каталоге.

```
src/fitness/                      ядро модуля
  xiaomi/                         CloudClient, Crypto, CurlTransport, HttpTransport,
                                  Credentials, Service, Normalize, Regions, DataKeys, Errors
  sync/SyncService.{hpp,cpp}
  Export.{hpp,cpp}                конверт schema_version 1.0 и CSV с экранированием
                                  формул (сейчас DataController::exportData и to_csv);
                                  разбор параметров запроса остается в контроллере
src/domain/fitness/Health.hpp
src/repositories/fitness/         восемь репозиториев, имена файлов как были
src/jobs/FitnessSyncHandler.hpp   тип задачи fitness_sync
src/api/FitnessController.{hpp,cpp}   один контроллер на все маршруты
migrations/011_fitness_credentials.sql
           012_fitness_health_data.sql
           013_fitness_sync_run_mutex.sql
           014_fitness_read_indexes.sql
           015_fitness_roles.sql
tests/unit/test_fitness_*.cpp
tests/integration/test_fitness_*.cpp
tests/api/test_fitness_api.cpp
tests/fitness/FakeHttpTransport.hpp
tests/fixtures/xiaomi_crypto_vectors.json
tools/fitness/                    gen_crypto_vectors.py, compare_with_python_bridge.py
docs/fitness/                     parity-report-2026-09.md, спека порта 2026-09-29
```

Включения меняются на новые пути: `"fitness/xiaomi/Service.hpp"`,
`"repositories/fitness/SleepRepository.hpp"`, `"domain/fitness/Health.hpp"`.
Пространства имен переносимого кода не меняются: `Xiaomi`, `Sync`,
`Domain`, `Repositories` (переименование без локального компилятора это
лишний риск ошибок, которые видны только в CI). Новые имена только у
нового кода: `Jobs::FitnessSync`, `Fitness::Export`, `Api::FitnessController`.
Тела `.cpp` компилируются в `app_core` через существующий GLOB, правок
CMake не нужно.

Имена таблиц не меняются (`xiaomi_credentials`, `daily_activity`,
`sleep_sessions`, `workouts`, `body_measurements`, `heart_rate_samples`,
`spo2_samples`, `stress_samples`, `abnormal_heart_beat_events`,
`sync_state`, `sync_runs`): данные прода потом переедут обычным
`pg_dump --data-only` без маппинга. Тела миграций 011..014 берутся из
mi-fitness-api как есть, меняется только заголовочный комментарий.

Ребра для `docs/module-deps.txt`. Гейт `check-module-deps.sh` берет
каталог включения целиком (отрезает только имя файла), поэтому вложенные
каталоги это отдельные узлы. Ожидаемый набор, точный список подскажет сам
гейт при первом прогоне:

```
api -> fitness            api -> fitness/sync       api -> fitness/xiaomi
api -> repositories/fitness
jobs -> fitness/sync      jobs -> fitness/xiaomi    jobs -> repositories/fitness
fitness -> repositories/fitness                     fitness -> utils
fitness/sync -> fitness/xiaomi                      fitness/sync -> repositories/fitness
fitness/sync -> database  fitness/sync -> utils
fitness/xiaomi -> domain/fitness                    fitness/xiaomi -> utils
repositories/fitness -> database                    repositories/fitness -> domain/fitness
repositories/fitness -> fitness/xiaomi              repositories/fitness -> utils
root -> repositories/fitness
```

Правило «utils/ включает только utils/» не задето: `fitness/xiaomi` берет
из хоста `utils/CurlInit.hpp`, а не наоборот.

## Модуль как выключатель

Модуль заводится штатным `./scripts/new-module.sh fitness`: ключ
`fitness.enabled` с env `FITNESS_ENABLED` (по умолчанию false), аксессор
`Core::fitness_enabled()` в `core/Modules.hpp`, строки в compose, обоих
чартах и `docs/CONFIG.md`. При выключенном модуле все маршруты отвечают 404
по образцу content-модуля, таймер расписания не регистрируется, посев
учетных данных не выполняется. Обработчик задачи `fitness_sync`
регистрируется всегда (воркер должен знать тип), при выключенном модуле
завершает задачу с ошибкой `fitness_disabled`.

## Конфигурация

JSON-блок `fitness`:

```json
"fitness": {
  "enabled": "${FITNESS_ENABLED:-false}",
  "xiaomi": {
    "token_key": "${MI_FITNESS_TOKEN_KEY:-}",
    "user_id": "${MI_FITNESS_USER_ID:-}",
    "pass_token": "${MI_FITNESS_PASS_TOKEN:-}",
    "region": "${MI_FITNESS_REGION:-cn}",
    "reseed": "${MI_FITNESS_RESEED:-false}",
    "sync_chunk_days": "${MI_FITNESS_CHUNK_DAYS:-7}",
    "sync_type_timeout_seconds": "${MI_FITNESS_SYNC_TYPE_TIMEOUT:-180}",
    "http_timeout_seconds": "${MI_FITNESS_HTTP_TIMEOUT:-20}",
    "sync_schedule_hours": "${MI_FITNESS_SYNC_SCHEDULE_HOURS:-0}",
    "sync_window_days": "${MI_FITNESS_SYNC_WINDOW_DAYS:-2}"
  }
}
```

Имена env остаются `MI_FITNESS_*`, чтобы Secret `mi-fitness-app` и ConfigMap
`mi-fitness-sync-tuning` из кластера подошли без правок. JSON-пути в коде
меняются на `fitness.xiaomi.*`. Описания ключей в `docs/CONFIG.md` берутся из
mi-fitness-api дословно, таблица `docs/config-sync-allowlist.txt` при
необходимости дополняется так же, как там. Helm-чарты не получают ключей
модуля кроме `FITNESS_ENABLED`: остальное приходит через `extraEnvFrom`.

## Маршруты

Все под `/api/v1/fitness/`, один контроллер `FitnessController`:

| Метод и путь | Право | Что делает |
|---|---|---|
| `GET probe` | fitness:sync | живая проверка учетных данных и связи, 409 при идущем синке |
| `POST sync` | fitness:sync | поставить синк диапазона в очередь |
| `GET sync/{id}` | fitness:sync | журнал запуска |
| `GET daily-activity` | fitness:read | суточная активность |
| `GET sleep` | fitness:read | сессии сна |
| `GET heart-rate` | fitness:read | пульс посэмплово |
| `GET stress` | fitness:read | стресс |
| `GET spo2` | fitness:read | SpO2 |
| `GET body` | fitness:read | замеры тела |
| `GET workouts` | fitness:read | тренировки |
| `GET abnormal-heart-beat` | fitness:read | события аномального пульса |
| `GET summary` | fitness:read | сводка по дням |
| `GET coverage` | fitness:read | покрытие по типам |
| `GET export` | fitness:read | json-конверт schema_version 1.0 или csv |

Параметры запросов, коды ошибок, форма ответов и ограничения (полуинтервал
по времени, потолок экспорта 366 дней, csv только для одного типа,
UTC-границы суток, окно сводки от полуночи региона) переносятся из
mi-fitness-api 1.9.2 без изменений: они выверены отчетом о паритете с
python-мостом. Пути в `docs/openapi.yaml` переписываются под новый префикс,
`operationId` получают префикс `fitness`, схемы переезжают как есть.
`Endpoints.hpp` получает 14 строк.

`--print-routes` и гейты маршрутов работают штатно: контроллер в
`src/api/`, макросы `ADD_METHOD_TO` в заголовке, тела в `.cpp`.

## Модель доступа

Два новых младших бита в `src/domain/Role.hpp` рядом с `kAuditRead = 0x02`:

```cpp
inline constexpr std::uint32_t kFitnessRead = 0x04;  // чтение /api/v1/fitness/*
inline constexpr std::uint32_t kFitnessSync = 0x08;  // probe и sync
```

Каждый метод контроллера начинается с проверки через макрос из
`src/api/Guards.hpp` (`Security::Auth::require_permission`). Администратор
проходит по сентинелу `kAdminister`, как везде в шаблоне. Аутентификация
не меняется: JWT-куки для людей, `X-API-Key` для машин; API-ключ наследует
права роли своего пользователя (`src/security/ApiKeys.hpp`), поэтому
отдельной модели ключей для модуля не нужно.

Миграция 015 сеет две роли `ON CONFLICT DO NOTHING`:

| Роль | Биты | Кому |
|---|---|---|
| Fitness Reader | 0x05 (GENERAL, FitnessRead) | агенты Life OS, читающие данные по API-ключу |
| Fitness Operator | 0x0D (GENERAL, FitnessRead, FitnessSync) | оператор синка |

Сценарий для агентов: админ создает пользователя с ролью Fitness Reader
через `POST /api/v1/admin/users`, входит им и выдает ключ через
`POST /api/v1/account/api-keys`. Регистрации самих пользователей в life-os-cpp
эта задача не касается.

## Обвязка хоста

1. `Core.cpp`: `register_fitness_sync_schedule_` (перенос
   `register_xiaomi_sync_schedule_`), вызывается из `init_jobs_` только при
   `fitness_enabled()`; читает `fitness.xiaomi.sync_schedule_hours` и
   `sync_window_days`, ставит задачу `fitness_sync` окном последних N суток.
2. `main.cpp`: после `Core::initialize` при `fitness_enabled()` вызывает
   `Repositories::Fitness::seed_credentials_if_missing()`.
3. `BuiltinHandlers.cpp`: регистрирует `Jobs::FitnessSync::kJobType`.
4. `Endpoints.hpp`, `Api.hpp`: 14 строк реестра и include контроллера.
5. `HealthController` не меняется; проб модуль не добавляет.
6. Probe и синк не одновременно: мьютекс запуска из миграции 013 и ответ
   409 переносятся как есть.

## Тесты

Файлы переезжают с переименованием `test_xiaomi_*` в `test_fitness_*`,
пространства имен и пути включений правятся. Бакеты по каталогам:

- unit: crypto (векторы из fixtures), login, fetch, transport, credentials,
  sync handler, normalize activity/sleep/rest, csv escape;
- integration: activity, sleep, credentials, rest repositories, health
  schema, sync service, sync schedule, seed;
- api: один `test_fitness_api.cpp` из трех прежних файлов, плюс новые
  случаи: 403 без бита `kFitnessRead` у пользователя с ролью User, 200 у
  Fitness Reader по API-ключу, 404 на любой маршрут при `FITNESS_ENABLED=false`;
- e2e: новых тестов нет (бакет требует полного цикла входа по HTTP, формы
  ответов покрывает бакет api); `tests/e2e/openapi.gen.json` перегенерируется,
  иначе падает проверка свежести.

`tests/unit/test_job_dispatch.cpp` получает проверку `fitness_sync`.

## Гейты и документация

После переноса зелеными должны быть все гейты из CLAUDE.md:
openapi-drift, routes-registered, test-buckets, version-sync,
frontend-nginx-sync, module-deps, config-sync, assemble-changelog,
lint-openapi, helm-lint, plus `check-selftest.sh` (гейты не менялись,
запуск для контроля). Из mi-fitness-api переносится правило в
`.gitleaks.toml` про значения `MI_FITNESS_*` в тестовых фикстурах, если оно
там есть (проверить diff двух файлов, они отличаются).

Документация: `docs/INDEX.md` получает строку про модуль, `docs/CONFIG.md`
раздел «Fitness module», `docs/openapi.yaml` новый тег `fitness`,
`README.md` life-os-cpp короткий раздел «Модуль fitness» с таблицей
маршрутов и ролями, CLAUDE.md раздел про запрет сборки и строку в
инвариантах про биты прав модуля. Changelog-фрагмент
`changelog.d/fitness.added.md`. Версия life-os-cpp после слияния 1.7.0.

## Проверка результата

1. CI life-os-cpp зеленый на ветке, включая санитайзеры и e2e.
2. `--print-routes` показывает 14 маршрутов `/api/v1/fitness/*`.
3. В api-тестах: пользователь без бита получает 403, Fitness Reader по
   ключу 200, при выключенном модуле 404.
4. Юнит-тесты крипты проходят на тех же векторах, что в mi-fitness-api,
   значит паритет протокола сохранен без повторной сверки с живым облаком.
