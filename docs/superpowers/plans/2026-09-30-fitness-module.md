# Fitness Module Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** перенести код синхронизации Mi Fitness из репо mi-fitness-api в life-os-cpp как модуль `fitness` с маршрутами `/api/v1/fitness/*` и правами на битах шаблона.

**Architecture:** код облака Xiaomi и синка ложится в `src/fitness/`, репозитории и домен в подкаталоги `fitness/` своих корзин, один контроллер `FitnessController` в `src/api/`, обработчик задачи `fitness_sync` в `src/jobs/`. Модуль включается флагом `FITNESS_ENABLED`, при выключенном модуле маршруты дают 404. Доступ по двум новым битам прав `kFitnessRead` и `kFitnessSync`.

**Tech Stack:** C++20, Drogon, PostgreSQL (pqxx), Redis, libsodium, libcurl, GoogleTest, CMake, шаблон cpp-rapid-rest-template 1.6.0.

**Spec:** `docs/superpowers/specs/2026-09-30-fitness-module-design.md`

## Global Constraints

- Локальная компиляция запрещена: ни `make test`, ни `make test-unit`, ни `make test-local`, ни `make build-local`, ни вызов компилятора. Сборка и тесты только в GitHub Actions. Локально только `make fmt`, `scripts/check-*.sh`, `make lint-openapi`, `make helm-lint`.
- Репо `/Users/moveeeax/Public/github/mi-fitness-api` только читается, ни одного изменения там.
- `LICENSE` life-os-cpp не трогать и нигде про лицензию не писать.
- Имена таблиц из mi-fitness-api не меняются. Имена env `MI_FITNESS_*` не меняются. JSON-пути в коде становятся `fitness.xiaomi.*`.
- Пространства имен `Xiaomi`, `Sync`, `Domain`, `Repositories` сохраняются как в исходнике (перенос без переименования снижает риск ошибок компиляции, которые видны только в CI). Новое: `Jobs::FitnessSync`, `Fitness::Export`, `Api::FitnessController`.
- Коммиты conventional commits, без AI-трейлеров. Ветка `feat/mi-fitness-module` в life-os-cpp.
- Все пути ниже относительно `/Users/moveeeax/Public/github/life-os-cpp`; `$MI` это `/Users/moveeeax/Public/github/mi-fitness-api`.
- `make fmt` требует clang-format 17; локально стоит 23. Перед первым `make fmt` выполнить `pip install clang-format==17.0.6` и убедиться, что `clang-format --version` отвечает 17 (или использовать `PATH="$(python3 -m site --user-base)/bin:$PATH"`).

## Review Focus

1. Пользователь с ролью User (биты 0x01) вызывает `GET /api/v1/fitness/sleep`: ожидается 403 с `required_permission`, а не 200 и не 500. Тест в Task 6.
2. `FITNESS_ENABLED=false` (значение по умолчанию) и любой маршрут модуля: ожидается 404 `fitness`, а не 503 `not_configured`. Тест в Task 6.
3. Воркер получает задачу `fitness_sync` при выключенном модуле: ожидается запись `failed` с `error: fitness_disabled` в журнале и результат без исключения, чтобы задача не ушла в DLQ бесконечными ретраями. Тест в Task 3.
4. `GET export?format=csv` без `type`: ожидается 400 `csv_needs_type`, а не пустое тело 200. Тест в Task 6.
5. Таймер расписания при `MI_FITNESS_SYNC_SCHEDULE_HOURS=6`, но `FITNESS_ENABLED=false`: таймер не регистрируется. Проверка в Task 3 через лог-строку в юнит-тесте невозможна без Core; закрывается кодом (`if (!fitness_enabled()) return;`) и повторной проверкой ревьюером, отмечено в Task 3.

---

### Task 1: Каркас модуля, биты прав, конфиг, правило сборки

**Files:**
- Modify: `src/domain/Role.hpp:30-32`
- Modify: `config/config.json`, `config/config.sample.json` (через `new-module.sh` плюс блок xiaomi)
- Modify: `src/core/Modules.hpp`, `docker/docker-compose.yml`, `helm/life-os-cpp/*`, `helm/life-os-cpp-worker/*`, `docs/CONFIG.md` (через `new-module.sh`)
- Create: `migrations/015_fitness_roles.sql` (создается здесь, применяется после 011..014 из Task 2, номер зарезервирован)
- Modify: `CLAUDE.md`
- Modify: `.gitleaks.toml`
- Test: `tests/unit/test_auth_permissions.cpp` (проверка новых битов)

**Interfaces:**
- Produces: `Domain::Permission::kFitnessRead = 0x04`, `Domain::Permission::kFitnessSync = 0x08`; `Core::fitness_enabled()`; JSON-ключи `fitness.enabled`, `fitness.xiaomi.*`.

- [ ] **Step 1: Убедиться, что ветка чистая**

```bash
cd /Users/moveeeax/Public/github/life-os-cpp
git status --short   # пусто
git branch --show-current   # feat/mi-fitness-module
```

- [ ] **Step 2: Прогнать штатный генератор модуля**

```bash
./scripts/new-module.sh fitness
git status --short
```

Ожидается: изменены `config/config.json`, `config/config.sample.json`, `src/core/Modules.hpp`, `docker/docker-compose.yml`, `helm/life-os-cpp/values.yaml`, `helm/life-os-cpp/templates/deployment.yaml`, `helm/life-os-cpp/templates/configmap.yaml`, те же три файла у `life-os-cpp-worker`, `docs/CONFIG.md`; создан `src/fitness/Fitness.hpp`. Файл `src/fitness/Fitness.hpp` удалить (`git rm -f` не нужен, он еще не в индексе: `rm src/fitness/Fitness.hpp`), модуль получит свои заголовки в Task 2.

- [ ] **Step 3: Добавить блок xiaomi в оба конфига**

В `config/config.json` и `config/config.sample.json` генератор создал `"fitness": { "enabled": "${FITNESS_ENABLED:-false}" }`. Дописать внутрь него ключ `xiaomi` так, чтобы блок стал:

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
  },
```

Проверить оба файла: `python3 -c "import json;json.load(open('config/config.json'));json.load(open('config/config.sample.json'))"`.

- [ ] **Step 4: Строки ключей в docs/CONFIG.md откладываются**

Генератор создал раздел `## Fitness module` с одной строкой `FITNESS_ENABLED`.
Десять строк `MI_FITNESS_*` добавляются в Task 2 вместе с кодом, который их
читает: гейт `check-config-sync.sh` считает строку документации без чтения в
`src/` устаревшей и красит коммит.

- [ ] **Step 5: Биты прав**

В `src/domain/Role.hpp` после строки `inline constexpr std::uint32_t kAuditRead = 0x02;` добавить:

```cpp
inline constexpr std::uint32_t kFitnessRead = 0x04;  // read /api/v1/fitness/* (data routes)
inline constexpr std::uint32_t kFitnessSync = 0x08;  // fitness probe + sync enqueue/status
```

- [ ] **Step 6: Тест на биты**

В `tests/unit/test_auth_permissions.cpp` найти существующий тест на `kAuditRead` (grep `kAuditRead`) и рядом добавить:

```cpp
TEST(PermissionBits, FitnessBitsAreDistinctLowBits) {
    using namespace Domain::Permission;
    EXPECT_EQ(kFitnessRead, 0x04u);
    EXPECT_EQ(kFitnessSync, 0x08u);
    EXPECT_EQ(kFitnessRead & kFitnessSync, 0u);
    EXPECT_EQ(kFitnessRead & kAuditRead, 0u);
    EXPECT_EQ(kFitnessSync & kAdminister, 0u);
    // Fitness Reader = 0x05, Fitness Operator = 0x0D (migration 015).
    EXPECT_EQ(kGeneral | kFitnessRead, 0x05u);
    EXPECT_EQ(kGeneral | kFitnessRead | kFitnessSync, 0x0Du);
}
```

Если в файле нет `#include "domain/Role.hpp"`, добавить.

- [ ] **Step 7: Миграция ролей**

Создать `migrations/015_fitness_roles.sql`:

```sql
-- Migration 015: fitness_roles — seed the two fitness roles.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.
--
-- Permission bits (src/domain/Role.hpp):
--   0x01 GENERAL, 0x04 FITNESS_READ, 0x08 FITNESS_SYNC.
-- Fitness Reader   = 0x05: read /api/v1/fitness/* — Life OS agents via API key.
-- Fitness Operator = 0x0D: reader + probe + sync enqueue/status.
-- Administrator passes every fitness guard through the kAdminister sentinel.

INSERT INTO roles (name, permissions, is_default) VALUES
    ('Fitness Reader',   5,  FALSE),
    ('Fitness Operator', 13, FALSE)
ON CONFLICT (name) DO NOTHING;
```

- [ ] **Step 8: Правило сборки в CLAUDE.md**

В `CLAUDE.md` после первой строки-заголовка `# CLAUDE.md — agent guide for this repo` вставить раздел дословно из `$MI/CLAUDE.md` (строки с `## Запрет: локальная сборка` по строку перед `C++20 REST service:`):

```bash
sed -n '/^## Запрет: локальная сборка/,/^C++20 REST service/p' "$MI/CLAUDE.md" | sed '$d' > /tmp/no-local-build.md
cat /tmp/no-local-build.md
```

Вставить после заголовка с пустой строкой до и после. В том же файле в раздел «Gate sequence» пункт 4 (`make test`) дополнить фразой: `In THIS repo step 4 runs only in CI (see the ban above); push and read the checks.`

- [ ] **Step 9: Правило gitleaks для криптовекторов**

Скопировать в `.gitleaks.toml` life-os-cpp блок из `$MI/.gitleaks.toml` (строки 43..51: комментарий и два значения `'''AAEC...'''`, `'''AQEB...'''`) в тот же список allowlist по значению, что и в исходнике (найти в life-os-cpp тот же `regexes = [` блок, куда в mi-fitness-api вставлены эти строки: `diff .gitleaks.toml $MI/.gitleaks.toml` показывает точное место). Путь фикстуры в комментарии оставить `tests/fixtures/xiaomi_crypto_vectors.json`, он совпадает.

- [ ] **Step 10: Гейты**

```bash
make fmt
./scripts/check-config-sync.sh && ./scripts/check-version-sync.sh && ./scripts/assemble-changelog.sh --check && ./scripts/check-module-deps.sh
make helm-lint
```

Ожидается: все зеленые. Ключи `fitness.xiaomi.*` в JSON без чтений в коде гейт не считает ошибкой; ошибкой были бы строки документации без чтений, поэтому они добавляются в Task 2.

- [ ] **Step 11: Коммит**

```bash
git add -A
git commit -m "feat(fitness): каркас модуля, биты прав, роли и конфиг"
```

---

### Task 2: Перенос ядра: xiaomi, sync, домен, репозитории, миграции, их тесты

**Files:**
- Create: `src/fitness/xiaomi/*` (18 файлов), `src/fitness/sync/SyncService.{hpp,cpp}`, `src/domain/fitness/Health.hpp`, `src/repositories/fitness/*Repository.hpp` (8 файлов)
- Create: `migrations/011_fitness_credentials.sql`, `012_fitness_health_data.sql`, `013_fitness_sync_run_mutex.sql`, `014_fitness_read_indexes.sql`
- Create: `tests/fitness/FakeHttpTransport.hpp`, `tests/fixtures/xiaomi_crypto_vectors.json`
- Create: `tests/unit/test_fitness_{credentials,crypto,fetch,login,transport,normalize_activity,normalize_rest,normalize_sleep}.cpp`
- Create: `tests/integration/test_fitness_{activity_repository,credentials_repository,health_schema,rest_repositories,sleep_repository,sync_service,seed}.cpp`
- Modify: `docs/module-deps.txt`
- Modify: `tests/test_helpers.hpp:341-347` (wipe таблиц модуля не нужен, но `wipe_app_data` не должен падать: таблицы модуля туда не добавляются)

**Interfaces:**
- Produces: `Xiaomi::*` (CloudClient, Crypto, CurlTransport, HttpTransport, Credentials, Service, Normalize, Regions, DataKeys, Errors) под `fitness/xiaomi/`; `Sync::SyncService`, `Sync::kAllDataTypes` под `fitness/sync/`; `Repositories::{Activity,Body,Credentials,HealthRead,Samples,Sleep,SyncRun,Workout}Repository` под `repositories/fitness/`; `Repositories::seed_xiaomi_credentials_if_missing()`; `Domain::DailyActivity` и остальные структуры под `domain/fitness/Health.hpp`.
- Consumes: JSON-ключи `fitness.xiaomi.*` из Task 1.

- [ ] **Step 1: Скопировать исходники**

```bash
cd /Users/moveeeax/Public/github/life-os-cpp
MI=/Users/moveeeax/Public/github/mi-fitness-api
mkdir -p src/fitness/xiaomi src/fitness/sync src/domain/fitness src/repositories/fitness tests/fitness tests/fixtures
cp "$MI"/src/xiaomi/* src/fitness/xiaomi/
cp "$MI"/src/sync/* src/fitness/sync/
cp "$MI"/src/domain/Health.hpp src/domain/fitness/Health.hpp
for r in Activity Body Credentials HealthRead Samples Sleep SyncRun Workout; do
  cp "$MI/src/repositories/${r}Repository.hpp" src/repositories/fitness/
done
cp "$MI"/tests/FakeHttpTransport.hpp tests/fitness/FakeHttpTransport.hpp
cp "$MI"/tests/fixtures/xiaomi_crypto_vectors.json tests/fixtures/
```

- [ ] **Step 2: Скопировать и переименовать тесты**

```bash
cp "$MI"/tests/unit/test_xiaomi_credentials.cpp   tests/unit/test_fitness_credentials.cpp
cp "$MI"/tests/unit/test_xiaomi_crypto.cpp        tests/unit/test_fitness_crypto.cpp
cp "$MI"/tests/unit/test_xiaomi_fetch.cpp         tests/unit/test_fitness_fetch.cpp
cp "$MI"/tests/unit/test_xiaomi_login.cpp         tests/unit/test_fitness_login.cpp
cp "$MI"/tests/unit/test_xiaomi_transport.cpp     tests/unit/test_fitness_transport.cpp
cp "$MI"/tests/unit/test_normalize_activity.cpp   tests/unit/test_fitness_normalize_activity.cpp
cp "$MI"/tests/unit/test_normalize_rest.cpp       tests/unit/test_fitness_normalize_rest.cpp
cp "$MI"/tests/unit/test_normalize_sleep.cpp      tests/unit/test_fitness_normalize_sleep.cpp
cp "$MI"/tests/integration/test_activity_repository.cpp    tests/integration/test_fitness_activity_repository.cpp
cp "$MI"/tests/integration/test_credentials_repository.cpp tests/integration/test_fitness_credentials_repository.cpp
cp "$MI"/tests/integration/test_health_schema.cpp          tests/integration/test_fitness_health_schema.cpp
cp "$MI"/tests/integration/test_rest_repositories.cpp      tests/integration/test_fitness_rest_repositories.cpp
cp "$MI"/tests/integration/test_sleep_repository.cpp       tests/integration/test_fitness_sleep_repository.cpp
cp "$MI"/tests/integration/test_sync_service.cpp           tests/integration/test_fitness_sync_service.cpp
cp "$MI"/tests/integration/test_xiaomi_seed.cpp            tests/integration/test_fitness_seed.cpp
```

Файлы `test_xiaomi_sync_handler.cpp`, `test_sync_schedule.cpp`, `test_csv_escape.cpp` и три api-теста в этой задаче не копируются: они переезжают в Task 3, 4 и 6.

- [ ] **Step 3: Переписать пути включений и ключи конфига**

```bash
FILES=$(find src/fitness src/domain/fitness src/repositories/fitness tests/fitness \
  tests/unit/test_fitness_*.cpp tests/integration/test_fitness_*.cpp -type f)
perl -pi -e '
  s{#include "xiaomi/}{#include "fitness/xiaomi/};
  s{#include "sync/}{#include "fitness/sync/};
  s{#include "domain/Health\.hpp"}{#include "domain/fitness/Health.hpp"};
  s{#include "repositories/((?:Activity|Body|Credentials|HealthRead|Samples|Sleep|SyncRun|Workout)Repository\.hpp)"}{#include "repositories/fitness/$1"};
  s{#include "FakeHttpTransport\.hpp"}{#include "fitness/FakeHttpTransport.hpp"};
  s{"xiaomi\.(token_key|user_id|pass_token|region|reseed|sync_chunk_days|sync_type_timeout_seconds|http_timeout_seconds|sync_schedule_hours|sync_window_days)"}{"fitness.xiaomi.$1"}g;
  s{cfg\["xiaomi"\]}{cfg["fitness"]["xiaomi"]}g;
' $FILES
grep -rn '#include "xiaomi/\|#include "sync/\|"xiaomi\.\|cfg\["xiaomi"\]' $FILES && echo "ОСТАЛИСЬ СТАРЫЕ ПУТИ" || echo "ok"
```

Ожидается `ok`. В `@file`-комментариях старые имена каталогов оставить как есть, гейты их не читают.

- [ ] **Step 4: Миграции**

```bash
cp "$MI/migrations/011_xiaomi_credentials.sql"  migrations/011_fitness_credentials.sql
cp "$MI/migrations/012_health_data.sql"         migrations/012_fitness_health_data.sql
cp "$MI/migrations/013_sync_run_mutex.sql"      migrations/013_fitness_sync_run_mutex.sql
cp "$MI/migrations/016_health_read_indexes.sql" migrations/014_fitness_read_indexes.sql
sed -i '' '1s/^-- Migration 011: xiaomi_credentials/-- Migration 011: fitness_credentials (xiaomi_credentials table)/' migrations/011_fitness_credentials.sql
sed -i '' '1s/^-- Migration 012: health_data/-- Migration 012: fitness_health_data/' migrations/012_fitness_health_data.sql
sed -i '' '1s/^-- Migration 013: sync_run_mutex/-- Migration 013: fitness_sync_run_mutex/' migrations/013_fitness_sync_run_mutex.sql
sed -i '' '1s/^-- Migration 016: health_read_indexes/-- Migration 014: fitness_read_indexes/' migrations/014_fitness_read_indexes.sql
head -1 migrations/01[1-4]_fitness_*.sql
grep -n 'mi_fitness_api\|RUN_MIGRATIONS_ONLY=1 \./' migrations/01[1-4]_fitness_*.sql
```

Упоминания бинарника `./mi_fitness_api` в комментариях заменить на `./life_os_cpp` (`sed -i '' 's#\./mi_fitness_api#./life_os_cpp#g' migrations/01[1-4]_fitness_*.sql`). Тела SQL не менять: гейт checksum считает по всему файлу, но эти базы новые, а прод-база mi_fitness сюда не подключается в этой задаче.

- [ ] **Step 5: Ребра module-deps**

В `docs/module-deps.txt` добавить, соблюдая сортировку файла:

```
fitness/sync -> database
fitness/sync -> fitness/xiaomi
fitness/sync -> repositories/fitness
fitness/sync -> utils
fitness/xiaomi -> domain/fitness
fitness/xiaomi -> utils
repositories/fitness -> database
repositories/fitness -> domain/fitness
repositories/fitness -> fitness/xiaomi
repositories/fitness -> utils
```

Затем прогнать гейт и добавить ровно те ребра, которые он назовет недостающими (например `repositories/fitness -> repositories/fitness` не нужно: одинаковые каталоги не считаются):

```bash
./scripts/check-module-deps.sh
```

- [ ] **Step 6: Строки ключей в docs/CONFIG.md**

Взять десять строк из `$MI/docs/CONFIG.md` (строки 253..262) с заменой пути `xiaomi.` на `fitness.xiaomi.`:

```bash
sed -n 253,262p "$MI/docs/CONFIG.md" | sed -E 's/`xiaomi\./`fitness.xiaomi./' > /tmp/fitness-config-rows.md
cat /tmp/fitness-config-rows.md
```

Вставить содержимое сразу после строки таблицы с `FITNESS_ENABLED` в разделе `## Fitness module` файла `docs/CONFIG.md`. Порядок столбцов сверить со строкой `FITNESS_ENABLED`; если он отличается от mi-fitness-api, переставить.

- [ ] **Step 7: Форматирование и гейты**

```bash
make fmt
./scripts/check-test-buckets.sh && ./scripts/check-module-deps.sh && ./scripts/check-config-sync.sh && ./scripts/check-version-sync.sh
```

Ожидается: зеленые. Если `check-config-sync.sh` не видит чтения вида `Config::get().get<int>("fitness.xiaomi.http_timeout_seconds", ...)` из `src/fitness/xiaomi/Service.cpp` (регулярка гейта ловит `cfg.get<T>(...)` и `Config::get().get<T>(...)`; в mi-fitness-api allowlist для этих ключей пуст, значит ловит), внести запись в `docs/config-sync-allowlist.txt` с комментарием по образцу соседних.

- [ ] **Step 8: Коммит**

```bash
git add -A
git commit -m "feat(fitness): перенос клиента Xiaomi, синка, репозиториев и миграций"
```

---

### Task 3: Обработчик задачи, расписание, посев учетных данных

**Files:**
- Create: `src/jobs/FitnessSyncHandler.hpp`
- Modify: `src/jobs/BuiltinHandlers.cpp`
- Modify: `src/core/Core.hpp`, `src/core/Core.cpp`
- Modify: `src/main.cpp:257-262` (функция `run_server`)
- Create: `tests/unit/test_fitness_sync_handler.cpp`, `tests/integration/test_fitness_sync_schedule.cpp`
- Modify: `tests/unit/test_job_dispatch.cpp:73-82`
- Modify: `docs/module-deps.txt`

**Interfaces:**
- Produces: `Jobs::FitnessSync::kJobType = "fitness_sync"`, `Jobs::FitnessSync::enqueue_recent(int window_days, long long now_epoch) -> long`, `Jobs::FitnessSync::process_job(const json&) -> json`; `Core::Application::register_fitness_sync_schedule_(Config::AppConfig&)`.
- Consumes: `Repositories::SyncRunRepository`, `Repositories::CredentialsRepository`, `Sync::SyncService`, `Xiaomi::Service` из Task 2; `Core::fitness_enabled()` из Task 1.

- [ ] **Step 1: Обработчик**

```bash
cp "$MI/src/jobs/XiaomiSyncHandler.hpp" src/jobs/FitnessSyncHandler.hpp
perl -pi -e '
  s{\@file XiaomiSyncHandler\.hpp}{\@file FitnessSyncHandler.hpp};
  s{xiaomi_sync}{fitness_sync}g;
  s{namespace Jobs::XiaomiSync}{namespace Jobs::FitnessSync}g;
  s{#include "repositories/((?:Credentials|SyncRun)Repository\.hpp)"}{#include "repositories/fitness/$1"};
  s{#include "sync/}{#include "fitness/sync/};
  s{#include "xiaomi/}{#include "fitness/xiaomi/};
  s{"xiaomi\.region"}{"fitness.xiaomi.region"}g;
' src/jobs/FitnessSyncHandler.hpp
```

Затем в `process_job` перед строкой `Repositories::SyncRunRepository runs;` вставить проверку выключенного модуля (Review Focus 3):

```cpp
    if (!Core::fitness_enabled()) {
        // Задача пришла в воркер с выключенным модулем: журнал получает честный
        // статус, а не бесконечные ретраи в DLQ.
        Repositories::SyncRunRepository runs_off;
        runs_off.finish(run_id, "failed", nlohmann::json{{"error", "fitness_disabled"}});
        return {{"run_id", run_id}, {"status", "failed"}};
    }
```

и добавить `#include "core/Modules.hpp"` в блок включений. Ребро `jobs -> core` в `docs/module-deps.txt` уже есть (проверить `grep '^jobs -> core' docs/module-deps.txt`, если нет, добавить).

- [ ] **Step 2: Регистрация обработчика**

В `src/jobs/BuiltinHandlers.cpp` добавить `#include "jobs/FitnessSyncHandler.hpp"` и в `register_builtin_handlers()` рядом с регистрацией `Webhooks::kJobType`:

```cpp
    d.register_handler(FitnessSync::kJobType, [](const json& payload) { return FitnessSync::process_job(payload); });
```

- [ ] **Step 3: Расписание в Core**

В `src/core/Core.hpp` внутри `class Application` в секции приватных инициализаторов после `register_queue_depth_metric_` добавить объявление:

```cpp
    // Плановый синк модуля fitness: раз в fitness.xiaomi.sync_schedule_hours
    // часов ставит в очередь окно последних sync_window_days суток. 0 выключает,
    // выключенный модуль тоже.
    static void register_fitness_sync_schedule_(Config::AppConfig& cfg);
```

В `src/core/Core.cpp`: добавить `#include "jobs/FitnessSyncHandler.hpp"`; в конце `init_jobs_` после `register_queue_depth_metric_(cfg);` добавить `register_fitness_sync_schedule_(cfg);`; после `init_jobs_` добавить тело:

```cpp
void Application::register_fitness_sync_schedule_(Config::AppConfig& cfg) {
    // Плановый синк живёт в API-поде: Tasks поднят только в серверном режиме,
    // сам синк исполняет воркер через очередь. runEvery дрогона стреляет
    // впервые через интервал, не при старте.
    if (!Tasks::is_initialized() || !Database::is_initialized() || !Jobs::is_initialized())
        return;
    if (!fitness_enabled())
        return;  // Review Focus 5: выключенный модуль не заводит таймер
    const int hours = cfg.get<int>("fitness.xiaomi.sync_schedule_hours", "MI_FITNESS_SYNC_SCHEDULE_HOURS", 0);
    if (hours <= 0)
        return;  // opt-in: без ручки расписания нет
    const int window = cfg.get<int>("fitness.xiaomi.sync_window_days", "MI_FITNESS_SYNC_WINDOW_DAYS", 2);
    spdlog::info("fitness sync schedule enabled: every {}h, window {} day(s)", hours, window);
    Tasks::schedule_recurring("fitness_sync_schedule", std::chrono::hours(hours), [window] {
        if (!Database::is_initialized() || !Jobs::is_initialized())
            return;
        try {
            Jobs::FitnessSync::enqueue_recent(window, static_cast<long long>(::time(nullptr)));
        } catch (const std::exception& e) {
            // База или очередь легли: тик пропущен, следующий повторит.
            spdlog::warn("fitness sync schedule tick failed: {}", e.what());
        }
    });
}
```

Проверить, что в Core.cpp есть `#include <chrono>` и `#include <ctime>` (для `::time`); если нет, добавить.

- [ ] **Step 4: Посев учетных данных в main.cpp**

В `src/main.cpp` в `run_server` после `Core::initialize(config_file);` вставить:

```cpp
    // Первый под в кластере сеет учётные данные Xiaomi из Secret; ротированный
    // токен в базе этот вызов не перетирает. Только при включённом модуле.
    if (Core::fitness_enabled()) {
        Repositories::seed_xiaomi_credentials_if_missing();
    }
```

и добавить `#include "repositories/fitness/CredentialsRepository.hpp"`. Ребро `root -> repositories/fitness` в `docs/module-deps.txt`. `Core::fitness_enabled()` доступен через `core/Core.hpp`, который main.cpp уже включает.

- [ ] **Step 5: Тесты обработчика и расписания**

```bash
cp "$MI/tests/unit/test_xiaomi_sync_handler.cpp" tests/unit/test_fitness_sync_handler.cpp
cp "$MI/tests/integration/test_sync_schedule.cpp" tests/integration/test_fitness_sync_schedule.cpp
perl -pi -e '
  s{jobs/XiaomiSyncHandler\.hpp}{jobs/FitnessSyncHandler.hpp}g;
  s{Jobs::XiaomiSync}{Jobs::FitnessSync}g;
  s{"xiaomi_sync"}{"fitness_sync"}g;
  s{#include "repositories/((?:Credentials|SyncRun)Repository\.hpp)"}{#include "repositories/fitness/$1"};
  s{#include "sync/}{#include "fitness/sync/};
  s{#include "xiaomi/}{#include "fitness/xiaomi/};
  s{#include "FakeHttpTransport\.hpp"}{#include "fitness/FakeHttpTransport.hpp"};
  s{cfg\["xiaomi"\]}{cfg["fitness"]["xiaomi"]}g;
' tests/unit/test_fitness_sync_handler.cpp tests/integration/test_fitness_sync_schedule.cpp
grep -n 'XiaomiSync\|xiaomi_sync' tests/unit/test_fitness_sync_handler.cpp tests/integration/test_fitness_sync_schedule.cpp && echo "ОСТАЛОСЬ" || echo ok
```

В `test_fitness_sync_schedule.cpp` фикстура `SyncScheduleTest::config_overrides` должна включать модуль: добавить `cfg["fitness"]["enabled"] = true;`. В `test_fitness_sync_handler.cpp` посмотреть, как он вызывает `process_job` (это юнит-тест без базы: вероятно проверяет `has_handler` после `register_builtin_handlers()` и разбор payload); если он вызывает `process_job` с реальной базой, оставить как есть, он лежит в бакете unit и в исходнике тоже лежал там.

Добавить в `tests/integration/test_fitness_sync_schedule.cpp` тест на выключенный модуль (Review Focus 3):

```cpp
class FitnessDisabledJobTest : public TestHelpers::CoreBackedTest {
protected:
    std::string config_file_name() const override { return "fitness_disabled_job_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        cfg["jobs"]["enabled"] = true;
        cfg["fitness"]["enabled"] = false;
    }
    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE sync_runs");
            return true;
        });
    }
};

TEST_F(FitnessDisabledJobTest, ProcessJobFinishesRunAsFailedWhenModuleIsOff) {
    Repositories::SyncRunRepository runs;
    const long run_id = runs.create("2026-09-21", "2026-09-22", Sync::kAllDataTypes);
    const auto result = Jobs::FitnessSync::process_job(
        json{{"run_id", run_id}, {"from", "2026-09-21"}, {"to", "2026-09-22"}});
    EXPECT_EQ(result["status"], "failed");
    const auto row = runs.get(run_id);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ((*row)["status"], "failed");
    EXPECT_EQ((*row)["error"]["error"], "fitness_disabled");
}
```

Поле, в котором `SyncRunRepository::finish` хранит json ошибки, проверить в `src/repositories/fitness/SyncRunRepository.hpp` (метод `finish(run_id, status, json)`) и подставить его имя вместо `"error"` во внешнем индексе, если оно другое. Добавить `#include "database/Database.hpp"` и `#include "fitness/sync/SyncService.hpp"` в этот тест, если их нет.

- [ ] **Step 6: test_job_dispatch**

В `tests/unit/test_job_dispatch.cpp` в тест `BuiltinHandlersAreRegistered` добавить `EXPECT_TRUE(d.has_handler(Jobs::FitnessSync::kJobType));` и `#include "jobs/FitnessSyncHandler.hpp"`.

- [ ] **Step 7: Гейты, коммит, первый прогон CI**

```bash
make fmt
./scripts/check-test-buckets.sh && ./scripts/check-module-deps.sh && ./scripts/check-config-sync.sh
git add -A
git commit -m "feat(fitness): задача fitness_sync, расписание и посев учетных данных"
git push -u origin feat/mi-fitness-module
gh pr create --draft --title "feat: модуль fitness (перенос mi-fitness-api)" --body "Перенос кода mi-fitness-api в life-os-cpp по спеке docs/superpowers/specs/2026-09-30-fitness-module-design.md. Черновик до зеленого CI."
gh pr checks --watch
```

Ожидается: джобы `build-and-test`, `clang-tidy`, `sanitizers`, `tsan` зеленые. При ошибках компиляции чинить по логу (`gh run view <id> --log-failed`), коммитить `fix(fitness): ...`, пушить, снова `gh pr checks --watch`. Никаких локальных сборок для проверки гипотез.

---

### Task 4: Экспорт: конверт и CSV вне контроллера

**Files:**
- Create: `src/fitness/Export.hpp`, `src/fitness/Export.cpp`
- Create: `tests/unit/test_fitness_csv_escape.cpp`
- Modify: `docs/module-deps.txt`

**Interfaces:**
- Produces: `Fitness::Export::kTypes` (`std::vector<std::string>`), `Fitness::Export::to_csv(const nlohmann::json& rows) -> std::string`, `Fitness::Export::envelope(const nlohmann::json& records, const std::string& type, const std::string& from, const std::string& to, long long now_epoch) -> nlohmann::json`.
- Consumes: `Xiaomi::detail::iso_with_offset` из `fitness/xiaomi/Normalize.hpp`.

- [ ] **Step 1: Тест**

```bash
cp "$MI/tests/unit/test_csv_escape.cpp" tests/unit/test_fitness_csv_escape.cpp
perl -pi -e '
  s{#include "api/DataController\.hpp"}{#include "fitness/Export.hpp"};
  s{Api::DataController::to_csv}{Fitness::Export::to_csv}g;
' tests/unit/test_fitness_csv_escape.cpp
```

Дописать в конец файла тест конверта:

```cpp
TEST(FitnessExport, EnvelopeMatchesBridgeSchema) {
    const json records = json{{"sleep", json::array()}};
    const json env = Fitness::Export::envelope(records, "sleep", "2026-09-01", "2026-09-02", 1790000000LL);
    EXPECT_EQ(env["schema_version"], "1.0");
    EXPECT_EQ(env["source"], "life-os-cpp");
    EXPECT_EQ(env["filters"]["dataset"], "sleep");
    EXPECT_EQ(env["filters"]["start_date"], "2026-09-01");
    EXPECT_EQ(env["filters"]["end_date"], "2026-09-02");
    EXPECT_TRUE(env["generated_at"].is_string());
    EXPECT_EQ(env["records"], records);
    const json all = Fitness::Export::envelope(records, "", "2026-09-01", "2026-09-02", 1790000000LL);
    EXPECT_TRUE(all["filters"]["dataset"].is_null());
}
```

- [ ] **Step 2: Заголовок**

`src/fitness/Export.hpp`:

```cpp
/**
 * @file Export.hpp
 * @brief Выгрузка данных здоровья: конверт schema_version 1.0 и CSV.
 * @details Не-HTTP часть бывшего DataController::exportData из mi-fitness-api:
 *          конверт совместим с python-мостом (export.py), CSV экранирует
 *          формулы по правилам _escape_csv_value. Контроллер только
 *          разбирает параметры и выбирает формат.
 */

#pragma once

#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace Fitness::Export {

/// Имена выгружаемых наборов, в порядке моста.
const std::vector<std::string>& types();

/// JSON-строки в CSV. Правила моста (_escape_csv_value): lstrip перед
/// проверкой, префиксы = + - @ TAB CR получают апостроф, экранируются только
/// строковые значения; числа не трогаются.
std::string to_csv(const nlohmann::json& rows);

/// Конверт выгрузки. @p type пустой означает все наборы (dataset = null).
nlohmann::json envelope(const nlohmann::json& records,
                        const std::string& type,
                        const std::string& from,
                        const std::string& to,
                        long long now_epoch);

}  // namespace Fitness::Export
```

- [ ] **Step 3: Тело**

`src/fitness/Export.cpp`:

```cpp
/**
 * @file Export.cpp
 * @brief Bodies for src/fitness/Export.hpp — compiled once into app_core.
 */

#include "fitness/Export.hpp"

#include <nlohmann/json.hpp>

#include "fitness/xiaomi/Normalize.hpp"

namespace Fitness::Export {

using json = nlohmann::json;

const std::vector<std::string>& types() {
    static const std::vector<std::string> kTypes = {"daily_activity",
                                                    "sleep",
                                                    "heart_rate",
                                                    "stress",
                                                    "spo2",
                                                    "body_measurements",
                                                    "workouts",
                                                    "abnormal_heart_beat"};
    return kTypes;
}

std::string to_csv(const json& rows) {
    if (!rows.is_array() || rows.empty()) {
        return "";
    }
    const auto cell = [](const json& v) {
        std::string s;
        bool escapable = false;
        if (v.is_null()) {
            s = "";
        } else if (v.is_string()) {
            s = v.get<std::string>();
            escapable = true;
        } else {
            s = v.dump();
        }
        if (escapable) {
            const auto lead = s.find_first_not_of(' ');
            if (lead != std::string::npos) {
                const char c = s[lead];
                if (c == '=' || c == '+' || c == '-' || c == '@' || c == '\t' || c == '\r') {
                    s.insert(s.begin(), '\'');
                }
            }
        }
        if (s.find_first_of(",\"\n\r") != std::string::npos) {
            std::string quoted = "\"";
            for (const char c : s) {
                if (c == '\"') {
                    quoted += "\"\"";
                } else {
                    quoted += c;
                }
            }
            quoted += "\"";
            return quoted;
        }
        return s;
    };
    std::string out;
    bool first = true;
    for (const auto& [key, value] : rows[0].items()) {
        (void)value;
        if (!first) {
            out += ',';
        }
        out += key;
        first = false;
    }
    out += '\n';
    for (const auto& row : rows) {
        first = true;
        for (const auto& [key, value] : row.items()) {
            (void)key;
            if (!first) {
                out += ',';
            }
            out += cell(value);
            first = false;
        }
        out += '\n';
    }
    return out;
}

json envelope(const json& records,
              const std::string& type,
              const std::string& from,
              const std::string& to,
              long long now_epoch) {
    // Конверт как у python-моста (export.py, schema_version 1.0): потребители
    // выгрузки не переучиваются.
    return json{{"schema_version", "1.0"},
                {"source", "life-os-cpp"},
                {"generated_at", Xiaomi::detail::iso_with_offset(now_epoch, 0)},
                {"filters", {{"dataset", type.empty() ? json() : json(type)}, {"start_date", from}, {"end_date", to}}},
                {"records", records}};
}

}  // namespace Fitness::Export
```

Проверить сигнатуру `Xiaomi::detail::iso_with_offset` в `src/fitness/xiaomi/Normalize.hpp` (`grep -n iso_with_offset`): она принимает epoch и смещение в секундах и возвращает `std::string`. Если она объявлена в `.cpp` без объявления в заголовке, добавить объявление в `Normalize.hpp` в `namespace Xiaomi::detail`.

- [ ] **Step 4: Ребра и гейты**

В `docs/module-deps.txt`: `fitness -> fitness/xiaomi`. Затем:

```bash
make fmt
./scripts/check-test-buckets.sh && ./scripts/check-module-deps.sh
```

- [ ] **Step 5: Коммит**

```bash
git add -A
git commit -m "feat(fitness): экспорт schema 1.0 и CSV вынесены из контроллера"
```

---

### Task 5: Контроллер, реестр маршрутов, OpenAPI

**Files:**
- Create: `src/api/FitnessController.hpp`, `src/api/FitnessController.cpp`
- Modify: `src/api/Endpoints.hpp:66` (после строки `DELETE /api/v1/jobs/{id}`)
- Modify: `src/api/Api.hpp` (include)
- Modify: `docs/openapi.yaml`, `tests/e2e/openapi.gen.json`
- Modify: `docs/module-deps.txt`

**Interfaces:**
- Produces: `Api::FitnessController` с методами `probe`, `syncEnqueue`, `syncStatus`, `dailyActivity`, `sleep`, `heartRate`, `stress`, `spo2`, `body`, `workouts`, `summary`, `abnormalHeartBeat`, `coverage`, `exportData`; сигнатуры `void m(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback)`, у `syncStatus` третий параметр `const std::string& id`.
- Consumes: `Fitness::Export` (Task 4), `Jobs::FitnessSync` (Task 3), репозитории и `Xiaomi::*` (Task 2), `Domain::Permission::kFitness*` и `Core::fitness_enabled()` (Task 1).

- [ ] **Step 1: Заголовок контроллера**

`src/api/FitnessController.hpp`:

```cpp
/**
 * @file FitnessController.hpp
 * @brief Модуль fitness: probe облака, постановка синка, чтение данных здоровья.
 *
 * Все маршруты под /api/v1/fitness. Порядок проверок в каждом методе:
 * выключенный модуль → 404 `fitness`; затем бит права (kFitnessSync для
 * probe и sync, kFitnessRead для чтения) → 403; затем разбор параметров → 400.
 *
 * Declarations only — the handler bodies live in FitnessController.cpp
 * (compiled once into app_core; ADR 0003 as amended 2026-08-22). The route
 * macros (ADD_METHOD_TO) must stay in this header: Drogon's METHOD_LIST
 * registration is part of the class definition, and
 * scripts/check-routes-registered.sh greps the src/api headers for them.
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

#include <nlohmann/json_fwd.hpp>

#include "repositories/fitness/HealthReadRepository.hpp"

namespace Api {

using namespace drogon;

class FitnessController : public HttpController<FitnessController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(FitnessController::probe, "/api/v1/fitness/probe", Get);
    ADD_METHOD_TO(FitnessController::syncEnqueue, "/api/v1/fitness/sync", Post);
    ADD_METHOD_TO(FitnessController::syncStatus, "/api/v1/fitness/sync/{id}", Get);
    ADD_METHOD_TO(FitnessController::dailyActivity, "/api/v1/fitness/daily-activity", Get);
    ADD_METHOD_TO(FitnessController::sleep, "/api/v1/fitness/sleep", Get);
    ADD_METHOD_TO(FitnessController::heartRate, "/api/v1/fitness/heart-rate", Get);
    ADD_METHOD_TO(FitnessController::stress, "/api/v1/fitness/stress", Get);
    ADD_METHOD_TO(FitnessController::spo2, "/api/v1/fitness/spo2", Get);
    ADD_METHOD_TO(FitnessController::body, "/api/v1/fitness/body", Get);
    ADD_METHOD_TO(FitnessController::workouts, "/api/v1/fitness/workouts", Get);
    ADD_METHOD_TO(FitnessController::summary, "/api/v1/fitness/summary", Get);
    ADD_METHOD_TO(FitnessController::abnormalHeartBeat, "/api/v1/fitness/abnormal-heart-beat", Get);
    ADD_METHOD_TO(FitnessController::coverage, "/api/v1/fitness/coverage", Get);
    ADD_METHOD_TO(FitnessController::exportData, "/api/v1/fitness/export", Get);
    METHOD_LIST_END

    void probe(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void syncEnqueue(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void syncStatus(const HttpRequestPtr& req,
                    std::function<void(const HttpResponsePtr&)>&& callback,
                    const std::string& id);
    void dailyActivity(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void sleep(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void heartRate(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void stress(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void spo2(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void body(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void workouts(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void summary(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void abnormalHeartBeat(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void coverage(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void exportData(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);

private:
    /// Разобранные параметры запроса чтения.
    struct Query {
        std::string from;
        std::string to;
        long limit = 1000;
        long offset = 0;
    };

    /// 404, если модуль выключен. Возвращает false после ответа.
    static bool require_enabled(const std::function<void(const HttpResponsePtr&)>& callback);

    /// Разобрать from/to/limit/offset. На ошибке отвечает 400 и возвращает false.
    static bool parse_query(const HttpRequestPtr& req,
                            Query& query,
                            const std::function<void(const HttpResponsePtr&)>& callback);

    /// Общий хвост чтения: выполнить и завернуть страницу в ответ.
    static void respond_page(const std::function<Repositories::HealthReadRepository::Page()>& read,
                             const std::function<void(const HttpResponsePtr&)>& callback);
};

}  // namespace Api
```

- [ ] **Step 2: Тело контроллера**

`src/api/FitnessController.cpp`. Тела probe, sync и чтения переносятся из `$MI/src/api/XiaomiController.hpp`, `SyncController.hpp`, `DataController.cpp` со следующими правками: включения, `Fitness::Export` вместо локального `to_csv` и списка типов, ключ региона `fitness.xiaomi.region`, два гарда в начале каждого метода.

```cpp
/**
 * @file FitnessController.cpp
 * @brief Bodies for src/api/FitnessController.hpp — compiled once into app_core.
 */

#include "api/FitnessController.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <optional>
#include <vector>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "api/Guards.hpp"
#include "api/RequestUtils.hpp"
#include "core/Modules.hpp"
#include "domain/Role.hpp"
#include "fitness/Export.hpp"
#include "fitness/sync/SyncService.hpp"
#include "fitness/xiaomi/CloudClient.hpp"
#include "fitness/xiaomi/DataKeys.hpp"
#include "fitness/xiaomi/Regions.hpp"
#include "fitness/xiaomi/Service.hpp"
#include "jobs/FitnessSyncHandler.hpp"
#include "jobs/Jobs.hpp"
#include "repositories/fitness/CredentialsRepository.hpp"
#include "repositories/fitness/SyncRunRepository.hpp"
#include "utils/Config.hpp"
#include "utils/ErrorResponse.hpp"

namespace Api {

using json = nlohmann::json;

namespace {

constexpr long kDefaultLimit = 1000;
constexpr long kMaxLimit = 10000;

/// Смещение суток региона: cn это UTC+8, прочие UTC (правило границ синка).
long long region_offset_seconds() {
    std::string region;
    if (Config::is_initialized()) {
        region = Config::get().get<std::string>("fitness.xiaomi.region", "MI_FITNESS_REGION", "");
    }
    return (region.empty() || region == "cn") ? 8 * 3600 : 0;
}

}  // namespace

// Гарды. Порядок: модуль → право → параметры. Макросы Guards.hpp заканчивают
// метод сами, поэтому они вызываются в теле метода, а не в хелпере.
#define FITNESS_GUARD(req, callback, perm)                    \
    do {                                                      \
        if (!require_enabled(callback))                       \
            return;                                           \
        API_REQUIRE_PERMISSION(req, callback, perm);          \
    } while (0)

bool FitnessController::require_enabled(const std::function<void(const HttpResponsePtr&)>& callback) {
    if (Core::fitness_enabled())
        return true;
    callback(ErrorResponse::not_found("fitness"));
    return false;
}

bool FitnessController::parse_query(const HttpRequestPtr& req,
                                    Query& query,
                                    const std::function<void(const HttpResponsePtr&)>& callback) {
    query.from = req->getParameter("from");
    query.to = req->getParameter("to");
    if (query.from.empty() || query.to.empty()) {
        callback(ErrorResponse::bad_request("invalid_range", "from and to are required as YYYY-MM-DD"));
        return false;
    }
    try {
        // Смещение пояса на проверку формата не влияет.
        Xiaomi::range_to_timestamps(query.from, query.to, "cn");
    } catch (const Xiaomi::MiFitnessProtocolError&) {
        callback(ErrorResponse::bad_request("invalid_range",
                                            "from and to must be YYYY-MM-DD and from must not be after to"));
        return false;
    }
    query.limit = kDefaultLimit;
    query.offset = 0;
    const std::string limit = req->getParameter("limit");
    const std::string offset = req->getParameter("offset");
    try {
        if (!limit.empty()) {
            query.limit = std::stol(limit);
        }
        if (!offset.empty()) {
            query.offset = std::stol(offset);
        }
    } catch (const std::exception&) {
        callback(ErrorResponse::bad_request("invalid_pagination", "limit and offset must be integers"));
        return false;
    }
    if (query.limit < 1 || query.limit > kMaxLimit || query.offset < 0) {
        callback(
            ErrorResponse::bad_request("invalid_pagination", "limit must be 1..10000 and offset must not be negative"));
        return false;
    }
    return true;
}

void FitnessController::respond_page(const std::function<Repositories::HealthReadRepository::Page()>& read,
                                     const std::function<void(const HttpResponsePtr&)>& callback) {
    try {
        const auto page = read();
        callback(Response::ok(json{{"data", page.rows}, {"count", page.rows.size()}, {"total", page.total}}));
    } catch (const std::exception& e) {
        // База лежит: состояние инфраструктуры, не 500 без следа в логе.
        spdlog::warn("fitness data read unavailable: {}", e.what());
        callback(ErrorResponse::service_unavailable("data_unavailable"));
    }
}

// ── probe ────────────────────────────────────────────────────────────────────

void FitnessController::probe(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessSync);
    // Синк и probe не живут одновременно: оба логинятся, а каждый логин
    // ротирует passToken. Живой запуск в журнале — probe отказывается
    // до похода в облако.
    try {
        if (Repositories::SyncRunRepository().any_running()) {
            callback(ErrorResponse::conflict("sync_in_progress", "a sync run is in progress, retry later"));
            return;
        }
    } catch (const std::exception&) {
        // Недоступная база не должна прятать probe: он сам упрётся в неё
        // ниже и ответит честнее.
    }

    const std::string key = req->getParameter("key");
    const std::string from = req->getParameter("from");
    const std::string to = req->getParameter("to");

    if (!Xiaomi::is_known_data_key(key)) {
        callback(ErrorResponse::bad_request("unknown_key", "key must be one of the Mi Fitness data keys"));
        return;
    }
    try {
        Xiaomi::range_to_timestamps(from, to, "cn");
    } catch (const Xiaomi::MiFitnessProtocolError&) {
        callback(ErrorResponse::bad_request("invalid_range",
                                            "from and to must be YYYY-MM-DD and from must not be after to"));
        return;
    }

    const std::string token_key = Xiaomi::Service::token_key_b64();
    if (token_key.empty()) {
        callback(ErrorResponse::service_unavailable("not_configured", "MI_FITNESS_TOKEN_KEY is not set"));
        return;
    }

    try {
        Repositories::CredentialsRepository repository(token_key);
        const auto credentials = repository.load();
        if (!credentials.has_value()) {
            callback(ErrorResponse::service_unavailable("not_configured", "Xiaomi credentials are not seeded yet"));
            return;
        }

        Xiaomi::CloudClient client(
            Xiaomi::Service::transport(), *credentials, [&repository](const Xiaomi::Credentials& rotated) {
                repository.store(rotated);
            });
        client.login();
        const auto items = client.fetch_key(key, from, to, std::nullopt);

        callback(Response::ok(json{{"data",
                                    {{"account", Xiaomi::mask_account_id(client.credentials().user_id)},
                                     {"region", client.credentials().region},
                                     {"key", key},
                                     {"records", items.size()}}}}));
    } catch (const Xiaomi::MiFitnessAuthError& e) {
        // Тексты MiFitness*Error по построению не содержат значений.
        spdlog::warn("fitness probe auth failure: {}", e.what());
        callback(ErrorResponse::service_unavailable("upstream_auth", "Xiaomi refused the stored credentials"));
    } catch (const Xiaomi::MiFitnessProtocolError& e) {
        spdlog::warn("fitness probe protocol failure: {}", e.what());
        callback(ErrorResponse::service_unavailable("upstream_protocol",
                                                    "Xiaomi response did not match the expected format"));
    }
}

// ── sync ─────────────────────────────────────────────────────────────────────

void FitnessController::syncEnqueue(const HttpRequestPtr& req,
                                    std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessSync);
    json body = json::parse(std::string(req->body()), nullptr, /*allow_exceptions=*/false);
    if (body.is_discarded() || !body.is_object() || !body.contains("from") || !body.contains("to") ||
        !body["from"].is_string() || !body["to"].is_string()) {
        callback(ErrorResponse::bad_request("invalid_body", "body must carry from and to as YYYY-MM-DD"));
        return;
    }
    const std::string from = body["from"].get<std::string>();
    const std::string to = body["to"].get<std::string>();
    try {
        Xiaomi::range_to_timestamps(from, to, "cn");
    } catch (const Xiaomi::MiFitnessProtocolError&) {
        callback(ErrorResponse::bad_request("invalid_range",
                                            "from and to must be YYYY-MM-DD and from must not be after to"));
        return;
    }
    std::vector<std::string> data_types = Sync::kAllDataTypes;
    if (body.contains("data_types")) {
        if (!body["data_types"].is_array() || body["data_types"].empty()) {
            callback(ErrorResponse::bad_request("invalid_data_types", "data_types must be a non-empty array"));
            return;
        }
        data_types.clear();
        for (const auto& item : body["data_types"]) {
            if (!item.is_string() ||
                std::find(Sync::kAllDataTypes.begin(), Sync::kAllDataTypes.end(), item.get<std::string>()) ==
                    Sync::kAllDataTypes.end()) {
                callback(ErrorResponse::bad_request("unknown_data_type", "data_types must be Mi Fitness types"));
                return;
            }
            if (std::find(data_types.begin(), data_types.end(), item.get<std::string>()) != data_types.end()) {
                // Дубль прогнал бы тип дважды и затёр запись результата.
                callback(ErrorResponse::bad_request("duplicate_data_type", "data_types must not repeat"));
                return;
            }
            data_types.push_back(item.get<std::string>());
        }
    }

    try {
        Repositories::SyncRunRepository runs;
        const long run_id = runs.create(from, to, data_types);
        Jobs::get().submit(Jobs::FitnessSync::kJobType,
                           json{{"run_id", run_id}, {"from", from}, {"to", to}, {"data_types", data_types}});
        auto resp = Response::ok(json{{"data", {{"run_id", run_id}, {"status", "queued"}}}});
        resp->setStatusCode(k202Accepted);
        callback(resp);
    } catch (const std::exception& e) {
        spdlog::warn("fitness sync enqueue unavailable: {}", e.what());
        callback(ErrorResponse::service_unavailable("queue_unavailable"));
    }
}

void FitnessController::syncStatus(const HttpRequestPtr& req,
                                   std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessSync);
    long run_id = 0;
    try {
        std::size_t consumed = 0;
        run_id = std::stol(id, &consumed);
        if (consumed != id.size() || run_id <= 0) {
            throw std::invalid_argument("trailing garbage");
        }
    } catch (const std::exception&) {
        callback(ErrorResponse::bad_request("invalid_id", "run id must be a positive integer"));
        return;
    }
    try {
        const auto row = Repositories::SyncRunRepository().get(run_id);
        if (!row.has_value()) {
            callback(ErrorResponse::not_found("run_not_found"));
            return;
        }
        callback(Response::ok(json{{"data", *row}}));
    } catch (const std::exception& e) {
        spdlog::warn("fitness sync status unavailable: {}", e.what());
        callback(ErrorResponse::service_unavailable("journal_unavailable"));
    }
}

// ── чтение ───────────────────────────────────────────────────────────────────

void FitnessController::dailyActivity(const HttpRequestPtr& req,
                                      std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    Query q;
    if (!parse_query(req, q, callback))
        return;
    respond_page([q] { return Repositories::HealthReadRepository().daily_activity(q.from, q.to, q.limit, q.offset); },
                 callback);
}

void FitnessController::sleep(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    Query q;
    if (!parse_query(req, q, callback))
        return;
    respond_page([q] { return Repositories::HealthReadRepository().sleep(q.from, q.to, q.limit, q.offset); }, callback);
}

void FitnessController::heartRate(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    Query q;
    if (!parse_query(req, q, callback))
        return;
    const std::string type = req->getParameter("type");
    if (!type.empty() && type != "passive" && type != "active" && type != "resting" && type != "manual") {
        callback(ErrorResponse::bad_request("invalid_type", "type must be passive, active, resting or manual"));
        return;
    }
    respond_page(
        [q, type] { return Repositories::HealthReadRepository().heart_rate(q.from, q.to, type, q.limit, q.offset); },
        callback);
}

void FitnessController::stress(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    Query q;
    if (!parse_query(req, q, callback))
        return;
    respond_page([q] { return Repositories::HealthReadRepository().stress(q.from, q.to, q.limit, q.offset); },
                 callback);
}

void FitnessController::spo2(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    Query q;
    if (!parse_query(req, q, callback))
        return;
    respond_page([q] { return Repositories::HealthReadRepository().spo2(q.from, q.to, q.limit, q.offset); }, callback);
}

void FitnessController::body(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    Query q;
    if (!parse_query(req, q, callback))
        return;
    respond_page([q] { return Repositories::HealthReadRepository().body(q.from, q.to, q.limit, q.offset); }, callback);
}

void FitnessController::workouts(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    Query q;
    if (!parse_query(req, q, callback))
        return;
    respond_page([q] { return Repositories::HealthReadRepository().workouts(q.from, q.to, q.limit, q.offset); },
                 callback);
}

void FitnessController::summary(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    Query q;
    if (!parse_query(req, q, callback))
        return;
    // Окна сна и resting-пульса строятся от локальной полуночи в поясе региона.
    const long long offset = region_offset_seconds();
    respond_page(
        [q, offset] { return Repositories::HealthReadRepository().summary(q.from, q.to, q.limit, q.offset, offset); },
        callback);
}

void FitnessController::abnormalHeartBeat(const HttpRequestPtr& req,
                                          std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    Query q;
    if (!parse_query(req, q, callback))
        return;
    respond_page(
        [q] { return Repositories::HealthReadRepository().abnormal_heart_beat(q.from, q.to, q.limit, q.offset); },
        callback);
}

void FitnessController::coverage(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    try {
        callback(Response::ok(json{{"data", Repositories::HealthReadRepository().coverage()}}));
    } catch (const std::exception& e) {
        spdlog::warn("fitness coverage unavailable: {}", e.what());
        callback(ErrorResponse::service_unavailable("data_unavailable"));
    }
}

void FitnessController::exportData(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    const auto& kTypes = Fitness::Export::types();
    Query q;
    if (!parse_query(req, q, callback))
        return;
    const std::string format = req->getParameter("format").empty() ? "json" : req->getParameter("format");
    const std::string type = req->getParameter("type");
    if (format != "json" && format != "csv") {
        callback(ErrorResponse::bad_request("invalid_format", "format must be json or csv"));
        return;
    }
    if (!type.empty() && std::find(kTypes.begin(), kTypes.end(), type) == kTypes.end()) {
        callback(ErrorResponse::bad_request("unknown_data_type", "type must be one of the exported datasets"));
        return;
    }
    // Без потолка ширины json_agg собирает годы данных одним значением в памяти пода.
    const auto range = Xiaomi::range_to_timestamps(q.from, q.to, "cn");
    if (range.second - range.first > 366LL * 86400) {
        callback(ErrorResponse::bad_request("range_too_wide", "export covers at most 366 days per request"));
        return;
    }
    if (format == "csv" && type.empty()) {
        // CSV это плоская таблица одного типа; все типы разом это JSON.
        callback(ErrorResponse::bad_request("csv_needs_type", "csv export takes exactly one type"));
        return;
    }
    try {
        Repositories::HealthReadRepository repo;
        if (format == "json") {
            json records = json::object();
            if (type.empty()) {
                for (const auto& t : kTypes) {
                    records[t] = repo.export_rows(t, q.from, q.to);
                }
            } else {
                records[type] = repo.export_rows(type, q.from, q.to);
            }
            const auto now =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                    .count();
            callback(Response::ok(Fitness::Export::envelope(records, type, q.from, q.to, now)));
            return;
        }
        const auto rows = repo.export_rows(type, q.from, q.to);
        auto resp = HttpResponse::newHttpResponse();
        resp->setContentTypeString("text/csv; charset=utf-8");
        resp->setBody(Fitness::Export::to_csv(rows));
        callback(resp);
    } catch (const std::exception& e) {
        spdlog::warn("fitness export unavailable: {}", e.what());
        callback(ErrorResponse::service_unavailable("data_unavailable"));
    }
}

#undef FITNESS_GUARD

}  // namespace Api
```

Проверить в `$MI/src/api/DataController.cpp`, что `Repositories::HealthReadRepository::Page` имеет поля `rows` и `total` (используются выше) и что `api/RequestUtils.hpp` life-os-cpp дает `Response::ok` (grep `Response::ok` в `src/api/RequestUtils.hpp`).

- [ ] **Step 3: Реестр и Api.hpp**

В `src/api/Endpoints.hpp` после строки `{"DELETE", "/api/v1/jobs/{id}", "Cancel job"},` добавить:

```cpp
        {"GET", "/api/v1/fitness/probe", "Fitness: live check of the Xiaomi credentials and cloud"},
        {"POST", "/api/v1/fitness/sync", "Fitness: enqueue a cloud sync run"},
        {"GET", "/api/v1/fitness/sync/{id}", "Fitness: read a sync run's journal entry"},
        {"GET", "/api/v1/fitness/daily-activity", "Fitness: daily activity rows for a date range"},
        {"GET", "/api/v1/fitness/sleep", "Fitness: sleep sessions for a date range"},
        {"GET", "/api/v1/fitness/heart-rate", "Fitness: heart rate samples for a date range"},
        {"GET", "/api/v1/fitness/stress", "Fitness: stress samples for a date range"},
        {"GET", "/api/v1/fitness/spo2", "Fitness: SpO2 samples for a date range"},
        {"GET", "/api/v1/fitness/body", "Fitness: body measurements for a date range"},
        {"GET", "/api/v1/fitness/workouts", "Fitness: workouts for a date range"},
        {"GET", "/api/v1/fitness/summary", "Fitness: per-day summary (steps, sleep, resting heart rate)"},
        {"GET", "/api/v1/fitness/abnormal-heart-beat", "Fitness: abnormal heart beat events for a date range"},
        {"GET", "/api/v1/fitness/coverage", "Fitness: per-type coverage (first/last date, records, last sync)"},
        {"GET", "/api/v1/fitness/export", "Fitness: export rows as a schema_version 1.0 json envelope or csv"},
```

В `src/api/Api.hpp` добавить `#include "api/FitnessController.hpp"` в алфавитном порядке среди контроллеров.

- [ ] **Step 4: OpenAPI**

Пути модуля в `$MI/docs/openapi.yaml` занимают строки 424..797, схемы `DailyActivityListResponse`..`ExportResponse` строки 198..385. Скопировать с заменой префиксов:

```bash
sed -n 424,797p "$MI/docs/openapi.yaml" \
  | sed -E 's#^  /api/v1/(xiaomi/probe|sync|data/)#  /api/v1/fitness/\1#; s#/api/v1/fitness/xiaomi/probe#/api/v1/fitness/probe#; s#/api/v1/fitness/data/#/api/v1/fitness/#; s#xiaomi_sync#fitness_sync#g; s#tags: \[(sync|data|xiaomi)\]#tags: [fitness]#' \
  > /tmp/fitness-paths.yaml
sed -n 198,385p "$MI/docs/openapi.yaml" > /tmp/fitness-schemas.yaml
grep -nE '^  /api/v1/' /tmp/fitness-paths.yaml
```

Ожидается: 14 путей, все начинаются с `/api/v1/fitness/`. Если в блоке есть `operationId`, добавить им префикс `fitness` (`sed -E 's/operationId: ([a-z])/operationId: fitness\U\1/'` в GNU sed; на macOS править руками). Вставить `/tmp/fitness-paths.yaml` в `docs/openapi.yaml` после последнего пути блока `/api/v1/jobs/...` (найти `grep -n '^  /api/v1/jobs' docs/openapi.yaml`, вставить после конца последнего такого блока, то есть перед следующей строкой вида `^  /api/v1/`). Вставить `/tmp/fitness-schemas.yaml` в конец `components.schemas` (перед `^paths:`). Если в life-os-cpp уже есть схема `Error`, а копия ссылается на нее, ничего добавлять не нужно; если имен `Error` нет, проверить, как называется схема ошибки в life-os-cpp (`grep -n 'ErrorResponse\|^    Error:' docs/openapi.yaml`) и заменить `$ref` в скопированных путях на нее.

Затем:

```bash
./scripts/gen-openapi-json.sh
./scripts/check-openapi-drift.sh && ./scripts/check-routes-registered.sh
make lint-openapi
```

`make lint-openapi` без локального spectral может использовать docker/npx (посмотреть цель в Makefile). Если инструмент недоступен, пропустить и опереться на CI-джобу lint.

- [ ] **Step 5: Ребра, форматирование, коммит**

В `docs/module-deps.txt`: `api -> fitness`, `api -> fitness/sync`, `api -> fitness/xiaomi`, `api -> repositories/fitness`, `api -> domain` (если нет), `api -> core` (если нет). Прогнать `./scripts/check-module-deps.sh` и добавить недостающие по его выводу.

```bash
make fmt
./scripts/check-openapi-drift.sh && ./scripts/check-routes-registered.sh && ./scripts/check-module-deps.sh && ./scripts/check-config-sync.sh
git add -A
git commit -m "feat(fitness): контроллер /api/v1/fitness, реестр маршрутов и OpenAPI"
```

---

### Task 6: API-тесты модуля

**Files:**
- Create: `tests/api/test_fitness_api.cpp`
- Test: тот же файл

**Interfaces:**
- Consumes: `Api::FitnessController` (Task 5), `TestHelpers::authed`, `TestHelpers::authed_json`, `TestHelpers::CoreBackedTest`, `Domain::Permission::kFitness*`, `Xiaomi::Service::install_for_testing`, `FakeHttpTransport`.

- [ ] **Step 1: Собрать файл из трех исходных**

```bash
{
  cat <<'EOF'
/**
 * @file test_fitness_api.cpp
 * @brief Маршруты модуля fitness: probe с подделкой транспорта, постановка
 *        синка, чтение данных, права и выключенный модуль.
 */

#include <cstdint>
#include <string>
#include <utility>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/FitnessController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "fitness/FakeHttpTransport.hpp"
#include "fitness/xiaomi/Service.hpp"
#include "jobs/Jobs.hpp"
#include "repositories/fitness/ActivityRepository.hpp"
#include "repositories/fitness/CredentialsRepository.hpp"
#include "repositories/fitness/SamplesRepository.hpp"
#include "repositories/fitness/SleepRepository.hpp"
#include "repositories/fitness/SyncRunRepository.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kTestKeyB64 = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";

Security::Auth::AuthPrincipal principal_with(std::uint32_t permissions) {
    Security::Auth::AuthPrincipal p;
    p.subject = "fitness-test-user";
    p.raw_claims = json{{"sub", p.subject}, {"permissions", permissions}};
    return p;
}

Security::Auth::AuthPrincipal reader() {
    return principal_with(Domain::Permission::kGeneral | Domain::Permission::kFitnessRead);
}

Security::Auth::AuthPrincipal operator_() {
    return principal_with(Domain::Permission::kGeneral | Domain::Permission::kFitnessRead |
                          Domain::Permission::kFitnessSync);
}

Security::Auth::AuthPrincipal plain_user() {
    return principal_with(Domain::Permission::kGeneral);
}

json body_of(const HttpResponsePtr& resp) {
    return json::parse(std::string(resp->body()));
}

}  // namespace
EOF
} > tests/api/test_fitness_api.cpp
```

- [ ] **Step 2: Перенести фикстуры и тесты из исходников**

Дописать в файл содержимое трех исходных тестов, начиная с их `namespace {` блоков фикстур и до конца, с правками:

```bash
sed -n '/^namespace {/,$p' "$MI/tests/api/test_xiaomi.cpp"   >> tests/api/test_fitness_api.cpp
sed -n '/^namespace {/,$p' "$MI/tests/api/test_sync.cpp"     >> tests/api/test_fitness_api.cpp
sed -n '/^namespace {/,$p' "$MI/tests/api/test_data_api.cpp" >> tests/api/test_fitness_api.cpp
perl -pi -e '
  s{Api::XiaomiController controller;}{Api::FitnessController controller;};
  s{Api::SyncController controller;}{Api::FitnessController controller;};
  s{Api::DataController controller;}{Api::FitnessController controller;};
  s{controller\.enqueue\(}{controller.syncEnqueue(}g;
  s{controller\.status\(}{controller.syncStatus(}g;
  s{cfg\["xiaomi"\]}{cfg["fitness"]["xiaomi"]}g;
  s{"xiaomi_sync"}{"fitness_sync"}g;
  s{Jobs::XiaomiSync}{Jobs::FitnessSync}g;
  s{TestHelpers::make_request\(Get\)}{TestHelpers::authed(operator_(), Get)}g;
  s{TestHelpers::make_request\(Post, body\)}{TestHelpers::authed_json(operator_(), body)}g;
' tests/api/test_fitness_api.cpp
```

Удалить из файла повторяющиеся `constexpr const char* kTestKeyB64`, `using json`, `using namespace drogon` и `body_of`, которые пришли из исходников (оставить только верхние). В каждой из трех фикстур (`XiaomiProbeTest`, `SyncApiTest`, `DataApiTest`) в `config_overrides` добавить `cfg["fitness"]["enabled"] = true;` (у `DataApiTest` метода нет, добавить `void config_overrides(nlohmann::json& cfg) override { cfg["fitness"]["enabled"] = true; }`). Проверить `grep -n 'make_request\|XiaomiController\|SyncController\|DataController' tests/api/test_fitness_api.cpp` дает пусто.

- [ ] **Step 3: Тесты прав и выключенного модуля**

Дописать в конец файла:

```cpp
// ── права и выключатель модуля ───────────────────────────────────────────────

namespace {

class FitnessGuardsTest : public TestHelpers::CoreBackedTest {
protected:
    Api::FitnessController controller;

    std::string config_file_name() const override { return "fitness_guards_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override { cfg["fitness"]["enabled"] = true; }

    HttpResponsePtr sleep_as(const Security::Auth::AuthPrincipal& p) {
        auto req = TestHelpers::authed(p, Get);
        req->setParameter("from", "2026-09-01");
        req->setParameter("to", "2026-09-02");
        HttpResponsePtr captured;
        controller.sleep(req, [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }

    HttpResponsePtr probe_as(const Security::Auth::AuthPrincipal& p) {
        auto req = TestHelpers::authed(p, Get);
        req->setParameter("key", "heart_rate");
        req->setParameter("from", "2026-09-01");
        req->setParameter("to", "2026-09-02");
        HttpResponsePtr captured;
        controller.probe(req, [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }
};

class FitnessDisabledTest : public TestHelpers::CoreBackedTest {
protected:
    Api::FitnessController controller;

    std::string config_file_name() const override { return "fitness_disabled_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override { cfg["fitness"]["enabled"] = false; }
};

}  // namespace

TEST_F(FitnessGuardsTest, PlainUserGets403OnRead) {
    // Review Focus 1: роль User (0x01) без бита kFitnessRead.
    const auto resp = sleep_as(plain_user());
    ASSERT_TRUE(resp);
    EXPECT_EQ(resp->statusCode(), k403Forbidden);
    EXPECT_EQ(body_of(resp)["error"], "forbidden");
    EXPECT_EQ(body_of(resp)["required_permission"], Domain::Permission::kFitnessRead);
}

TEST_F(FitnessGuardsTest, ReaderGets200OnReadAnd403OnProbe) {
    const auto ok = sleep_as(reader());
    ASSERT_TRUE(ok);
    EXPECT_EQ(ok->statusCode(), k200OK);
    const auto denied = probe_as(reader());
    ASSERT_TRUE(denied);
    EXPECT_EQ(denied->statusCode(), k403Forbidden);
}

TEST_F(FitnessGuardsTest, AdminPassesEveryGuard) {
    const auto resp = sleep_as(principal_with(Domain::Permission::kAdminister));
    ASSERT_TRUE(resp);
    EXPECT_EQ(resp->statusCode(), k200OK);
}

TEST_F(FitnessGuardsTest, CsvExportWithoutTypeIs400) {
    // Review Focus 4.
    auto req = TestHelpers::authed(reader(), Get);
    req->setParameter("from", "2026-09-01");
    req->setParameter("to", "2026-09-02");
    req->setParameter("format", "csv");
    HttpResponsePtr captured;
    controller.exportData(req, [&](const HttpResponsePtr& r) { captured = r; });
    ASSERT_TRUE(captured);
    EXPECT_EQ(captured->statusCode(), k400BadRequest);
    EXPECT_EQ(body_of(captured)["error"], "csv_needs_type");
}

TEST_F(FitnessDisabledTest, EveryRouteIs404WhenModuleIsOff) {
    // Review Focus 2: даже администратор и даже без учётных данных.
    auto req = TestHelpers::authed(principal_with(Domain::Permission::kAdminister), Get);
    req->setParameter("from", "2026-09-01");
    req->setParameter("to", "2026-09-02");
    HttpResponsePtr captured;
    controller.sleep(req, [&](const HttpResponsePtr& r) { captured = r; });
    ASSERT_TRUE(captured);
    EXPECT_EQ(captured->statusCode(), k404NotFound);
    EXPECT_EQ(body_of(captured)["error"], "fitness");

    captured.reset();
    controller.coverage(TestHelpers::authed(principal_with(Domain::Permission::kAdminister), Get),
                        [&](const HttpResponsePtr& r) { captured = r; });
    ASSERT_TRUE(captured);
    EXPECT_EQ(captured->statusCode(), k404NotFound);
}
```

Если `ErrorResponse::not_found("fitness")` кладет аргумент не в поле `error`, а в `message` (проверить `src/utils/ErrorResponse.hpp`), поправить ожидание на нужное поле.

- [ ] **Step 4: Гейты, коммит, второй прогон CI**

```bash
make fmt
./scripts/check-test-buckets.sh && ./scripts/check-module-deps.sh
git add -A
git commit -m "test(fitness): api-тесты маршрутов, прав и выключенного модуля"
git push
gh pr checks --watch
```

Ожидается: `build-and-test` зеленый, включая бакеты unit, integration, api и e2e (тест `OpenApiSpec.GenJsonIsFreshAndLoadable` проходит после `gen-openapi-json.sh` из Task 5). Чинить по логам, коммитить `fix(fitness): ...`, пушить, снова `gh pr checks --watch`. Никаких локальных сборок.

---

### Task 7: Документация, инструменты, changelog, релиз 1.7.0

**Files:**
- Create: `tools/fitness/gen_crypto_vectors.py`, `tools/fitness/compare_with_python_bridge.py`
- Create: `docs/fitness/parity-report-2026-09.md`, `docs/fitness/2026-09-29-mi-fitness-api-cpp-design.md`
- Modify: `docs/INDEX.md`, `README.md`, `CLAUDE.md`
- Create: `changelog.d/fitness.added.md`
- Modify (через `release.sh`): `CMakeLists.txt`, helm-пины, `CHANGELOG.md`

- [ ] **Step 1: Инструменты и история**

```bash
mkdir -p tools/fitness docs/fitness
cp "$MI"/tools/*.py tools/fitness/
cp "$MI"/docs/parity-report-2026-09.md docs/fitness/
cp "$MI"/docs/superpowers/specs/2026-09-29-mi-fitness-api-cpp-design.md docs/fitness/
grep -n 'tests/fixtures\|tools/' tools/fitness/*.py | head
```

Пути внутри скриптов, указывающие на `tools/gen_crypto_vectors.py` или на `tests/fixtures/...`, оставить: фикстура лежит по тому же пути. В начале `docs/fitness/parity-report-2026-09.md` добавить строку: `Перенесено из mi-fitness-api 1.9.2 без изменений; маршруты в life-os-cpp живут под /api/v1/fitness/.`

- [ ] **Step 2: INDEX и README**

В `docs/INDEX.md` в таблицу «Worked examples & deep-dives» добавить строку:

```
| [`fitness/parity-report-2026-09.md`](fitness/parity-report-2026-09.md) | Модуль fitness (Mi Fitness → Postgres): сверка порта с python-мостом; спека порта рядом в `fitness/` |
```

В `README.md` перед разделом `## Contributing` добавить:

```markdown
## Модуль fitness

Синхронизация данных Mi Fitness (Xiaomi Cloud) в Postgres, перенесена из
[mi-fitness-api](https://github.com/moveeeax/mi-fitness-api). Включается
`FITNESS_ENABLED=true`, учетные данные и ручки синка через `MI_FITNESS_*`
(см. `docs/CONFIG.md`, раздел «Fitness module»). Маршруты под
`/api/v1/fitness/`:

| Маршрут | Право | Что делает |
|---|---|---|
| `GET probe` | fitness:sync | живая проверка учетных данных и связи с облаком |
| `POST sync`, `GET sync/{id}` | fitness:sync | поставить синк в очередь, прочитать журнал |
| `GET daily-activity`, `sleep`, `heart-rate`, `stress`, `spo2`, `body`, `workouts`, `abnormal-heart-beat` | fitness:read | данные за диапазон дат |
| `GET summary`, `coverage`, `export` | fitness:read | сводка по дням, покрытие, выгрузка json/csv |

Права это биты `kFitnessRead` (0x04) и `kFitnessSync` (0x08) в
`src/domain/Role.hpp`; миграция 015 сеет роли «Fitness Reader» и
«Fitness Operator». API-ключ наследует права роли своего пользователя,
поэтому агенту хватает пользователя с ролью Reader и ключа из
`POST /api/v1/account/api-keys`.
```

- [ ] **Step 3: CLAUDE.md**

В разделе «Invariants the CI gates enforce» после пункта 10 добавить:

```
11. **Fitness module:** feature bits `kFitnessRead`/`kFitnessSync` guard every
    `/api/v1/fitness/*` handler (Guards.hpp `API_REQUIRE_PERMISSION` after the
    `Core::fitness_enabled()` 404 check); config keys live under
    `fitness.xiaomi.*` with the historical `MI_FITNESS_*` env names; table
    names match mi-fitness-api so prod data moves by plain pg_dump.
```

- [ ] **Step 4: Changelog-фрагмент**

`changelog.d/fitness.added.md`:

```
Модуль `fitness`: перенос синхронизации Mi Fitness из mi-fitness-api 1.9.2
(клиент облака Xiaomi, синк, восемь типов данных здоровья, выгрузка schema
1.0/csv) под `/api/v1/fitness/*` с битами прав `kFitnessRead`/`kFitnessSync`
и ролями Fitness Reader/Operator; выключатель `FITNESS_ENABLED`.
```

- [ ] **Step 5: Гейты и коммит**

```bash
make fmt
./scripts/assemble-changelog.sh --check && ./scripts/check-version-sync.sh
./scripts/check-selftest.sh   # гейты не менялись, прогон для контроля (нужны helm и yq)
git add -A
git commit -m "docs(fitness): инструменты, отчет о паритете, README и changelog"
git push
gh pr checks --watch
```

- [ ] **Step 6: Релиз 1.7.0 после зеленого CI**

```bash
./scripts/release.sh 1.7.0
./scripts/check-version-sync.sh
git add -A
git commit -m "chore(release): 1.7.0 — модуль fitness"
git push
gh pr checks --watch
gh pr ready
```

Ожидается: все проверки PR зеленые. Слияние и тег `v1.7.0` делает владелец.
