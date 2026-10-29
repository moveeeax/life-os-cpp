/**
 * @file ConfigKeys.hpp
 * @brief The registry of configuration keys: one (json path, env name) pair
 *        per knob the code reads.
 *
 * The registry is complete (one entry per knob), but most call sites still
 * spell the literal path and env pair in their get<T> call; only a few read
 * through an entry, as `cfg.get(Config::Keys::jobs_enabled, false)` would.
 * Both forms are accepted.
 * The default always lives at the call site, because it depends on the type
 * the site reads and scripts/check-config-sync.sh compares the defaults of two
 * sites that read the same knob. That gate also parses this file: every entry
 * below must be read somewhere in src/ (as `Keys::<name>` or as a literal
 * get<T> triple of path and env), and every literal triple must have an entry
 * here, so the registry is complete and never stale. The names are the json
 * path with dots as underscores; an env-only knob (no json path) is named
 * after its env in lower case.
 *
 * Generated from the triples the gate extracted when the registry was
 * introduced (wave 3 of the project review); kept by hand since.
 */

#pragma once

namespace Config {

/// One configuration knob: the dot path in config.json and the environment
/// variable that overrides it. Either may be empty (an env-only knob has no
/// path), never both.
struct ConfigKey {
    const char* path;
    const char* env;
};

namespace Keys {

// --- (env-only) ---
inline constexpr ConfigKey redis_sentinel_nodes{"", "REDIS_SENTINEL_NODES"};
inline constexpr ConfigKey worker_types{"", "WORKER_TYPES"};

// --- api ---
inline constexpr ConfigKey api_public_paths{"api.public_paths", "API_PUBLIC_PATHS"};
inline constexpr ConfigKey api_public_paths_extra{"api.public_paths_extra", "API_PUBLIC_PATHS_EXTRA"};

// --- app ---
inline constexpr ConfigKey app_base_url{"app.base_url", "APP_BASE_URL"};
inline constexpr ConfigKey app_env{"app.env", "APP_ENV"};
inline constexpr ConfigKey app_name{"app.name", "APP_NAME"};

// --- auth ---
inline constexpr ConfigKey auth_bearer_token{"auth.bearer_token", "AUTH_BEARER_TOKEN"};
inline constexpr ConfigKey auth_cookies_access_name{"auth.cookies.access_name", "AUTH_COOKIE_ACCESS"};
inline constexpr ConfigKey auth_cookies_access_ttl_sec{"auth.cookies.access_ttl_sec", "AUTH_COOKIE_ACCESS_TTL_SEC"};
inline constexpr ConfigKey auth_cookies_enabled{"auth.cookies.enabled", "AUTH_COOKIES_ENABLED"};
inline constexpr ConfigKey auth_cookies_refresh_name{"auth.cookies.refresh_name", "AUTH_COOKIE_REFRESH"};
inline constexpr ConfigKey auth_cookies_refresh_revocation_prefix{"auth.cookies.refresh_revocation_prefix",
                                                                  "AUTH_COOKIE_REVOCATION_PREFIX"};
inline constexpr ConfigKey auth_cookies_refresh_ttl_sec{"auth.cookies.refresh_ttl_sec", "AUTH_COOKIE_REFRESH_TTL_SEC"};
inline constexpr ConfigKey auth_cookies_samesite{"auth.cookies.samesite", "AUTH_COOKIE_SAMESITE"};
inline constexpr ConfigKey auth_cookies_secure{"auth.cookies.secure", "AUTH_COOKIE_SECURE"};
inline constexpr ConfigKey auth_jwt_audience{"auth.jwt.audience", "JWT_AUDIENCE"};
inline constexpr ConfigKey auth_jwt_issuer{"auth.jwt.issuer", "JWT_ISSUER"};
inline constexpr ConfigKey auth_jwt_leeway_sec{"auth.jwt.leeway_sec", "JWT_LEEWAY_SEC"};
inline constexpr ConfigKey auth_jwt_roles_claim{"auth.jwt.roles_claim", "JWT_ROLES_CLAIM"};
inline constexpr ConfigKey auth_jwt_scopes_claim{"auth.jwt.scopes_claim", "JWT_SCOPES_CLAIM"};
inline constexpr ConfigKey auth_jwt_secret{"auth.jwt.secret", "JWT_SECRET"};
inline constexpr ConfigKey auth_mode{"auth.mode", "AUTH_MODE"};
inline constexpr ConfigKey auth_registration_open{"auth.registration_open", "REGISTRATION_OPEN"};

// --- billing ---
inline constexpr ConfigKey billing_credits_per_unit{"billing.credits_per_unit", "BILLING_CREDITS_PER_UNIT"};
inline constexpr ConfigKey billing_currency{"billing.currency", "BILLING_CURRENCY"};
inline constexpr ConfigKey billing_enabled{"billing.enabled", "BILLING_ENABLED"};
inline constexpr ConfigKey billing_max_amount_cents{"billing.max_amount_cents", "BILLING_MAX_AMOUNT_CENTS"};
inline constexpr ConfigKey billing_min_amount_cents{"billing.min_amount_cents", "BILLING_MIN_AMOUNT_CENTS"};
inline constexpr ConfigKey billing_paypal_cancel_url{"billing.paypal.cancel_url", "PAYPAL_CANCEL_URL"};
inline constexpr ConfigKey billing_paypal_client_id{"billing.paypal.client_id", "PAYPAL_CLIENT_ID"};
inline constexpr ConfigKey billing_paypal_client_secret{"billing.paypal.client_secret", "PAYPAL_CLIENT_SECRET"};
inline constexpr ConfigKey billing_paypal_environment{"billing.paypal.environment", "PAYPAL_ENV"};
inline constexpr ConfigKey billing_paypal_return_url{"billing.paypal.return_url", "PAYPAL_RETURN_URL"};
inline constexpr ConfigKey billing_paypal_webhook_id{"billing.paypal.webhook_id", "PAYPAL_WEBHOOK_ID"};

// --- cache ---
inline constexpr ConfigKey cache_db{"cache.db", "REDIS_DB"};
inline constexpr ConfigKey cache_password{"cache.password", "REDIS_PASSWORD"};
inline constexpr ConfigKey cache_pool_size{"cache.pool_size", "CACHE_POOL_SIZE"};
inline constexpr ConfigKey cache_pool_wait_timeout_ms{"cache.pool_wait_timeout_ms", "REDIS_POOL_WAIT_TIMEOUT_MS"};
inline constexpr ConfigKey cache_sentinel_master_name{"cache.sentinel.master_name", "REDIS_MASTER_NAME"};
inline constexpr ConfigKey cache_sentinel_password{"cache.sentinel.password", "REDIS_SENTINEL_PASSWORD"};
inline constexpr ConfigKey cache_socket_timeout_ms{"cache.socket_timeout_ms", "REDIS_SOCKET_TIMEOUT_MS"};
inline constexpr ConfigKey cache_url{"cache.url", "REDIS_URL"};
inline constexpr ConfigKey cache_use_sentinel{"cache.use_sentinel", "REDIS_USE_SENTINEL"};

// --- cors ---
inline constexpr ConfigKey cors_allowed_origins{"cors.allowed_origins", "CORS_ALLOWED_ORIGINS"};

// --- database ---
inline constexpr ConfigKey database_acquire_timeout_ms{"database.acquire_timeout_ms", "DB_ACQUIRE_TIMEOUT_MS"};
inline constexpr ConfigKey database_host{"database.host", "DATABASE_HOST"};
inline constexpr ConfigKey database_migrations_dir{"database.migrations_dir", "DB_MIGRATIONS_DIR"};
inline constexpr ConfigKey database_migrations_enabled{"database.migrations_enabled", "DB_MIGRATIONS_ENABLED"};
inline constexpr ConfigKey database_name{"database.name", "DATABASE_NAME"};
inline constexpr ConfigKey database_password{"database.password", "DATABASE_PASSWORD"};
inline constexpr ConfigKey database_pool_metric_refresh_sec{"database.pool_metric_refresh_sec",
                                                            "DB_POOL_METRIC_REFRESH_SEC"};
inline constexpr ConfigKey database_pool_size{"database.pool_size", "DB_POOL_SIZE"};
inline constexpr ConfigKey database_port{"database.port", "DATABASE_PORT"};
inline constexpr ConfigKey database_primary{"database.primary", "DATABASE_PRIMARY_URL"};
inline constexpr ConfigKey database_replica_lag_metric_refresh_sec{"database.replica_lag_metric_refresh_sec",
                                                                   "DB_REPLICA_LAG_METRIC_REFRESH_SEC"};
inline constexpr ConfigKey database_retry_base_delay_ms{"database.retry.base_delay_ms", "DB_RETRY_BASE_DELAY_MS"};
inline constexpr ConfigKey database_retry_jitter{"database.retry.jitter", "DB_RETRY_JITTER"};
inline constexpr ConfigKey database_retry_max_attempts{"database.retry.max_attempts", "DB_RETRY_MAX_ATTEMPTS"};
inline constexpr ConfigKey database_retry_max_delay_ms{"database.retry.max_delay_ms", "DB_RETRY_MAX_DELAY_MS"};
inline constexpr ConfigKey database_statement_timeout_ms{"database.statement_timeout_ms", "DB_STATEMENT_TIMEOUT_MS"};
inline constexpr ConfigKey database_user{"database.user", "DATABASE_USER"};

// --- docs ---
inline constexpr ConfigKey docs_enabled{"docs.enabled", "DOCS_ENABLED"};
inline constexpr ConfigKey docs_openapi_path{"docs.openapi_path", "DOCS_OPENAPI_PATH"};

// --- fitness ---
inline constexpr ConfigKey fitness_enabled{"fitness.enabled", "FITNESS_ENABLED"};
inline constexpr ConfigKey fitness_xiaomi_http_timeout_seconds{"fitness.xiaomi.http_timeout_seconds",
                                                               "MI_FITNESS_HTTP_TIMEOUT"};
inline constexpr ConfigKey fitness_xiaomi_sync_chunk_days{"fitness.xiaomi.sync_chunk_days", "MI_FITNESS_CHUNK_DAYS"};
inline constexpr ConfigKey fitness_xiaomi_sync_schedule_hours{"fitness.xiaomi.sync_schedule_hours",
                                                              "MI_FITNESS_SYNC_SCHEDULE_HOURS"};
inline constexpr ConfigKey fitness_xiaomi_sync_type_timeout_seconds{"fitness.xiaomi.sync_type_timeout_seconds",
                                                                    "MI_FITNESS_SYNC_TYPE_TIMEOUT"};
inline constexpr ConfigKey fitness_xiaomi_sync_window_days{"fitness.xiaomi.sync_window_days",
                                                           "MI_FITNESS_SYNC_WINDOW_DAYS"};
inline constexpr ConfigKey fitness_xiaomi_token_key{"fitness.xiaomi.token_key", "MI_FITNESS_TOKEN_KEY"};

// --- food ---
inline constexpr ConfigKey food_enabled{"food.enabled", "FOOD_ENABLED"};

// --- goals ---
inline constexpr ConfigKey goals_enabled{"goals.enabled", "GOALS_ENABLED"};

// --- habits ---
inline constexpr ConfigKey habits_enabled{"habits.enabled", "HABITS_ENABLED"};

// --- idempotency ---
inline constexpr ConfigKey idempotency_enabled{"idempotency.enabled", "IDEMPOTENCY_ENABLED"};
inline constexpr ConfigKey idempotency_lock_ttl_sec{"idempotency.lock_ttl_sec", "IDEMPOTENCY_LOCK_TTL_SEC"};
inline constexpr ConfigKey idempotency_max_body_kb{"idempotency.max_body_kb", "IDEMPOTENCY_MAX_BODY_KB"};
inline constexpr ConfigKey idempotency_max_response_kb{"idempotency.max_response_kb", "IDEMPOTENCY_MAX_RESPONSE_KB"};
inline constexpr ConfigKey idempotency_ttl_sec{"idempotency.ttl_sec", "IDEMPOTENCY_TTL_SEC"};

// --- jobs ---
inline constexpr ConfigKey jobs_enabled{"jobs.enabled", "JOBS_ENABLED"};
inline constexpr ConfigKey jobs_max_retries{"jobs.max_retries", "JOBS_MAX_RETRIES"};
inline constexpr ConfigKey jobs_result_ttl{"jobs.result_ttl", "JOBS_RESULT_TTL"};
inline constexpr ConfigKey jobs_retry_backoff_base_ms{"jobs.retry_backoff_base_ms", "JOBS_RETRY_BACKOFF_BASE_MS"};
inline constexpr ConfigKey jobs_retry_backoff_max_ms{"jobs.retry_backoff_max_ms", "JOBS_RETRY_BACKOFF_MAX_MS"};
inline constexpr ConfigKey jobs_visibility_timeout_sec{"jobs.visibility_timeout_sec", "JOBS_VISIBILITY_TIMEOUT_SEC"};

// --- logging ---
inline constexpr ConfigKey logging_file{"logging.file", "LOG_FILE"};
inline constexpr ConfigKey logging_format{"logging.format", "LOG_FORMAT"};
inline constexpr ConfigKey logging_level{"logging.level", "LOG_LEVEL"};
inline constexpr ConfigKey logging_name{"logging.name", "LOG_NAME"};

// --- mail ---
inline constexpr ConfigKey mail_enabled{"mail.enabled", "MAIL_ENABLED"};
inline constexpr ConfigKey mail_from{"mail.from", "MAIL_FROM"};
inline constexpr ConfigKey mail_from_name{"mail.from_name", "MAIL_FROM_NAME"};
inline constexpr ConfigKey mail_smtp_host{"mail.smtp_host", "MAIL_SMTP_HOST"};
inline constexpr ConfigKey mail_smtp_password{"mail.smtp_password", "MAIL_SMTP_PASSWORD"};
inline constexpr ConfigKey mail_smtp_port{"mail.smtp_port", "MAIL_SMTP_PORT"};
inline constexpr ConfigKey mail_smtp_use_tls{"mail.smtp_use_tls", "MAIL_SMTP_USE_TLS"};
inline constexpr ConfigKey mail_smtp_username{"mail.smtp_username", "MAIL_SMTP_USERNAME"};
inline constexpr ConfigKey mail_subject_prefix{"mail.subject_prefix", "MAIL_SUBJECT_PREFIX"};
inline constexpr ConfigKey mail_templates_dir{"mail.templates_dir", "MAIL_TEMPLATES_DIR"};
inline constexpr ConfigKey mail_timeout_sec{"mail.timeout_sec", "MAIL_TIMEOUT_SEC"};
inline constexpr ConfigKey mail_via_jobs{"mail.via_jobs", "MAIL_VIA_JOBS"};

// --- medical ---
inline constexpr ConfigKey medical_enabled{"medical.enabled", "MEDICAL_ENABLED"};

// --- money ---
inline constexpr ConfigKey money_advisor_hour_utc{"money.advisor_hour_utc", "MONEY_ADVISOR_HOUR_UTC"};
inline constexpr ConfigKey money_enabled{"money.enabled", "MONEY_ENABLED"};
inline constexpr ConfigKey money_llm_model_vision{"money.llm.model_vision", "MONEY_LLM_MODEL_VISION"};
inline constexpr ConfigKey money_rates_schedule_hours{"money.rates_schedule_hours", "MONEY_RATES_SCHEDULE_HOURS"};

// --- notes ---
inline constexpr ConfigKey notes_enabled{"notes.enabled", "NOTES_ENABLED"};

// --- ani agent ---
inline constexpr ConfigKey ani_agent_enabled{"ani.agent.enabled", "ANI_AGENT_ENABLED"};
inline constexpr ConfigKey ani_agent_image{"ani.agent.image", "ANI_AGENT_IMAGE"};
inline constexpr ConfigKey ani_agent_keeper_image{"ani.agent.keeper_image", "ANI_AGENT_KEEPER_IMAGE"};
inline constexpr ConfigKey ani_agent_namespace{"ani.agent.namespace", "ANI_AGENT_NAMESPACE"};
inline constexpr ConfigKey ani_agent_node_label{"ani.agent.node_label", "ANI_AGENT_NODE_LABEL"};
inline constexpr ConfigKey ani_agent_max_running{"ani.agent.max_running", "ANI_AGENT_MAX_RUNNING"};
inline constexpr ConfigKey ani_agent_idle_minutes{"ani.agent.idle_minutes", "ANI_AGENT_IDLE_MINUTES"};
inline constexpr ConfigKey ani_agent_max_hours{"ani.agent.max_hours", "ANI_AGENT_MAX_HOURS"};
inline constexpr ConfigKey ani_agent_model{"ani.agent.model", "ANI_AGENT_MODEL"};
inline constexpr ConfigKey ani_agent_upstream_url{"ani.agent.upstream_url", "ANI_AGENT_UPSTREAM_URL"};
inline constexpr ConfigKey ani_agent_upstream_key{"ani.agent.upstream_key", "ANI_AGENT_UPSTREAM_KEY"};
inline constexpr ConfigKey ani_agent_proxy_url{"ani.agent.proxy_url", "ANI_AGENT_PROXY_URL"};
inline constexpr ConfigKey ani_agent_api_url{"ani.agent.api_url", "ANI_AGENT_API_URL"};
inline constexpr ConfigKey ani_agent_monthly_tokens{"ani.agent.monthly_tokens", "ANI_AGENT_MONTHLY_TOKENS"};
inline constexpr ConfigKey ani_telegram_url{"ani.telegram.url", "ANI_TELEGRAM_URL"};
inline constexpr ConfigKey ani_telegram_token{"ani.telegram.token", "ANI_TELEGRAM_TOKEN"};
inline constexpr ConfigKey calendar_google_client_id{"calendar.google.client_id", "GOOGLE_CALENDAR_CLIENT_ID"};
inline constexpr ConfigKey calendar_google_client_secret{"calendar.google.client_secret", "GOOGLE_CALENDAR_CLIENT_SECRET"};
inline constexpr ConfigKey calendar_google_redirect_url{"calendar.google.redirect_url", "GOOGLE_CALENDAR_REDIRECT_URL"};
inline constexpr ConfigKey calendar_google_token_key{"calendar.google.token_key", "CALENDAR_TOKEN_KEY"};

// --- now ---
inline constexpr ConfigKey now_enabled{"now.enabled", "NOW_ENABLED"};
inline constexpr ConfigKey now_ani_voice_api_key{"now.ani_voice.api_key", "ANI_VOICE_API_KEY"};
inline constexpr ConfigKey now_ani_voice_model{"now.ani_voice.model", "ANI_VOICE_MODEL"};
inline constexpr ConfigKey now_ani_voice_tts_model{"now.ani_voice.tts_model", "ANI_VOICE_TTS_MODEL"};
inline constexpr ConfigKey now_ani_voice_voice{"now.ani_voice.voice", "ANI_VOICE_VOICE"};

// --- observability ---
inline constexpr ConfigKey observability_metrics_address{"observability.metrics_address", "METRICS_ADDRESS"};
inline constexpr ConfigKey observability_otlp_endpoint{"observability.otlp_endpoint", "OTLP_ENDPOINT"};
inline constexpr ConfigKey observability_service_name{"observability.service_name", "SERVICE_NAME"};
inline constexpr ConfigKey observability_trace_stdout{"observability.trace_stdout", "TRACE_STDOUT"};

// --- outbox ---
inline constexpr ConfigKey outbox_drain_interval_sec{"outbox.drain_interval_sec", "OUTBOX_DRAIN_INTERVAL_SEC"};

// --- practice ---
inline constexpr ConfigKey practice_enabled{"practice.enabled", "PRACTICE_ENABLED"};

// --- rate_limit ---
inline constexpr ConfigKey rate_limit_auth_fail_requests{"rate_limit.auth_fail_requests",
                                                         "RATE_LIMIT_AUTH_FAIL_REQUESTS"};
inline constexpr ConfigKey rate_limit_auth_fail_window_sec{"rate_limit.auth_fail_window_sec",
                                                           "RATE_LIMIT_AUTH_FAIL_WINDOW_SEC"};
inline constexpr ConfigKey rate_limit_enabled{"rate_limit.enabled", "RATE_LIMIT_ENABLED"};
inline constexpr ConfigKey rate_limit_login_email_requests{"rate_limit.login_email_requests",
                                                           "RATE_LIMIT_LOGIN_EMAIL_REQUESTS"};
inline constexpr ConfigKey rate_limit_login_email_window_sec{"rate_limit.login_email_window_sec",
                                                             "RATE_LIMIT_LOGIN_EMAIL_WINDOW_SEC"};
inline constexpr ConfigKey rate_limit_fail_open{"rate_limit.fail_open", "RATE_LIMIT_FAIL_OPEN"};
inline constexpr ConfigKey rate_limit_protected_paths{"rate_limit.protected_paths", "RATE_LIMIT_PROTECTED_PATHS"};
inline constexpr ConfigKey rate_limit_protected_requests{"rate_limit.protected_requests",
                                                         "RATE_LIMIT_PROTECTED_REQUESTS"};
inline constexpr ConfigKey rate_limit_protected_window_sec{"rate_limit.protected_window_sec",
                                                           "RATE_LIMIT_PROTECTED_WINDOW_SEC"};
inline constexpr ConfigKey rate_limit_requests{"rate_limit.requests", "RATE_LIMIT_REQUESTS"};
inline constexpr ConfigKey rate_limit_scope{"rate_limit.scope", "RATE_LIMIT_SCOPE"};
inline constexpr ConfigKey rate_limit_trust_proxy{"rate_limit.trust_proxy", "RATE_LIMIT_TRUST_PROXY"};
inline constexpr ConfigKey rate_limit_trusted_proxy_count{"rate_limit.trusted_proxy_count",
                                                          "RATE_LIMIT_TRUSTED_PROXY_COUNT"};
inline constexpr ConfigKey rate_limit_whitelist{"rate_limit.whitelist", "RATE_LIMIT_WHITELIST"};
inline constexpr ConfigKey rate_limit_window_sec{"rate_limit.window_sec", "RATE_LIMIT_WINDOW_SEC"};

// --- security ---
inline constexpr ConfigKey security_csrf_cookie_name{"security.csrf.cookie_name", "SECURITY_CSRF_COOKIE"};
inline constexpr ConfigKey security_csrf_enabled{"security.csrf.enabled", "SECURITY_CSRF_ENABLED"};
inline constexpr ConfigKey security_csrf_header_name{"security.csrf.header_name", "SECURITY_CSRF_HEADER"};
inline constexpr ConfigKey security_hsts{"security.hsts", "SECURITY_HSTS"};
inline constexpr ConfigKey security_hsts_max_age{"security.hsts_max_age", "SECURITY_HSTS_MAX_AGE"};

// --- server ---
inline constexpr ConfigKey server_host{"server.host", "SERVER_HOST"};
inline constexpr ConfigKey server_max_body_bytes{"server.max_body_bytes", "SERVER_MAX_BODY_BYTES"};
inline constexpr ConfigKey server_port{"server.port", "SERVER_PORT"};
inline constexpr ConfigKey server_ssl_cert{"server.ssl.cert", "SSL_CERT_FILE"};
inline constexpr ConfigKey server_ssl_enabled{"server.ssl.enabled", "SERVER_SSL_ENABLED"};
inline constexpr ConfigKey server_ssl_key{"server.ssl.key", "SSL_KEY_FILE"};
inline constexpr ConfigKey server_threads{"server.threads", "SERVER_THREADS"};

// --- shutdown ---
inline constexpr ConfigKey shutdown_pre_stop_delay_sec{"shutdown.pre_stop_delay_sec", "SHUTDOWN_PRE_STOP_DELAY_SEC"};

// --- storage ---
inline constexpr ConfigKey storage_s3_access_key_id{"storage.s3.access_key_id", "S3_ACCESS_KEY_ID"};
inline constexpr ConfigKey storage_s3_bucket{"storage.s3.bucket", "S3_BUCKET"};
inline constexpr ConfigKey storage_s3_endpoint{"storage.s3.endpoint", "S3_ENDPOINT"};
inline constexpr ConfigKey storage_s3_path_style{"storage.s3.path_style", "S3_PATH_STYLE"};
inline constexpr ConfigKey storage_s3_prefix{"storage.s3.prefix", "S3_PREFIX"};
inline constexpr ConfigKey storage_s3_public_base_url{"storage.s3.public_base_url", "S3_PUBLIC_BASE_URL"};
inline constexpr ConfigKey storage_s3_region{"storage.s3.region", "S3_REGION"};
inline constexpr ConfigKey storage_s3_secret_access_key{"storage.s3.secret_access_key", "S3_SECRET_ACCESS_KEY"};

// --- tasks ---
inline constexpr ConfigKey tasks_enabled{"tasks.enabled", "TASKS_ENABLED"};
inline constexpr ConfigKey tasks_llm_prompt_parse{"tasks.llm.prompt_parse", "TASKS_LLM_PROMPT_PARSE"};

// --- travel ---
inline constexpr ConfigKey travel_enabled{"travel.enabled", "TRAVEL_ENABLED"};

// --- worker ---
inline constexpr ConfigKey worker_brpop_timeout{"worker.brpop_timeout", "WORKER_BRPOP_TIMEOUT"};
inline constexpr ConfigKey worker_concurrency{"worker.concurrency", "WORKER_CONCURRENCY"};
inline constexpr ConfigKey worker_health_port{"worker.health_port", "WORKER_HEALTH_PORT"};
inline constexpr ConfigKey worker_id{"worker.id", "WORKER_ID"};
inline constexpr ConfigKey worker_strict_types{"worker.strict_types", "WORKER_STRICT_TYPES"};

// --- workout ---
inline constexpr ConfigKey workout_enabled{"workout.enabled", "WORKOUT_ENABLED"};

}  // namespace Keys

}  // namespace Config
