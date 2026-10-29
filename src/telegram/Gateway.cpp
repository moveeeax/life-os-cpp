/**
 * @file Gateway.cpp
 * @brief Body for src/telegram/Gateway.hpp.
 */

#include "telegram/Gateway.hpp"

#include <cstdio>

#include "utils/Config.hpp"

namespace Telegram {

namespace {

using json = nlohmann::json;

}  // namespace

std::optional<Settings> settings() {
    if (!Config::is_initialized()) {
        return std::nullopt;
    }
    auto& cfg = Config::get();
    Settings s;
    s.token = cfg.get<std::string>("ani.telegram.token", "ANI_TELEGRAM_TOKEN", "");
    s.url = cfg.get<std::string>("ani.telegram.url", "ANI_TELEGRAM_URL", "https://tg.tarassov.me");
    while (!s.url.empty() && s.url.back() == '/') {
        s.url.pop_back();
    }
    if (s.token.empty() || s.url.empty()) {
        return std::nullopt;
    }
    return s;
}

json Reply::data() const {
    if (body.is_object() && body.contains("data")) {
        return body["data"];
    }
    return body;
}

bool valid_id(const std::string& id) {
    const std::size_t start = !id.empty() && id[0] == '-' ? 1 : 0;
    const std::size_t digits = id.size() - start;
    if (digits < 1 || digits > 20) {
        return false;
    }
    for (std::size_t i = start; i < id.size(); ++i) {
        if (id[i] < '0' || id[i] > '9') {
            return false;
        }
    }
    return true;
}

std::string encode(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (const unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

std::string text_of(const json& message) {
    const json content = message.value("content", json::object());
    if (!content.is_object()) {
        return {};
    }
    for (const char* key : {"text", "caption"}) {
        const auto it = content.find(key);
        if (it != content.end() && it->is_string() && !it->get<std::string>().empty()) {
            return it->get<std::string>();
        }
    }
    const std::string type = content.value("type", "");
    return type.empty() || type == "text" ? std::string() : "[" + type + "]";
}

Reply call(Net::Http::Transport& transport,
           const Settings& s,
           const std::string& method,
           const std::string& path,
           const Query& query,
           const json& body,
           const std::string& idempotency_key) {
    Net::Http::Request r;
    r.method = method;
    r.url = s.url + path;
    char sep = '?';
    for (const auto& [k, v] : query) {
        r.url += sep + k + "=" + encode(v);
        sep = '&';
    }
    r.headers = {{"Authorization", "Bearer " + s.token}, {"Accept", "application/json"}};
    if (!idempotency_key.empty()) {
        r.headers.emplace_back("Idempotency-Key", idempotency_key);
    }
    if (!body.is_null()) {
        r.body = body.dump();
        r.content_type = "application/json";
    }
    r.timeout_seconds = 15;
    Reply out;
    try {
        const auto res = transport.send(r);
        out.status = res.status;
        out.body = json::parse(res.body, nullptr, /*allow_exceptions=*/false);
        if (out.body.is_discarded()) {
            out.body = nullptr;
        }
    } catch (const Net::Http::TransportError&) {
        out.status = 0;
    }
    return out;
}

}  // namespace Telegram
