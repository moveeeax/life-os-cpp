/**
 * @file Service.cpp
 * @brief Тела шва: боевой транспорт и чтение настроек.
 */

#include "fitness/xiaomi/Service.hpp"

#include "fitness/xiaomi/CurlTransport.hpp"
#include "utils/Config.hpp"

namespace Xiaomi::Service {

namespace {

HttpTransport*& override_slot() {
    static HttpTransport* slot = nullptr;
    return slot;
}

}  // namespace

HttpTransport& transport() {
    if (override_slot() != nullptr) {
        return *override_slot();
    }
    // Таймаут читается один раз при первом обращении: конфиг к этому моменту
    // уже инициализирован, оба бинаря поднимают его до первого запроса.
    static CurlTransport production(http_timeout_seconds());
    return production;
}

void install_for_testing(HttpTransport* transport) {
    override_slot() = transport;
}

long http_timeout_seconds() {
    if (!Config::is_initialized()) {
        return 20;
    }
    return Config::get().get<int>("fitness.xiaomi.http_timeout_seconds", "MI_FITNESS_HTTP_TIMEOUT", 20);
}

std::string token_key_b64() {
    if (!Config::is_initialized()) {
        return {};
    }
    return Config::get().get<std::string>("fitness.xiaomi.token_key", "MI_FITNESS_TOKEN_KEY", "");
}

}  // namespace Xiaomi::Service
