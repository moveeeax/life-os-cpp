/**
 * @file Hook.cpp
 * @brief Bodies for src/agent/Hook.hpp.
 */

#include "agent/Hook.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

#include "now/Voice.hpp"

namespace Agent::Hook {

namespace {

using json = nlohmann::json;

std::size_t chars(std::string_view s) {
    return static_cast<std::size_t>(
        std::count_if(s.begin(), s.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

std::string_view trim(std::string_view s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string_view::npos) {
        return {};
    }
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

/// Text of 1..@p max characters after trimming, without control characters
/// other than line breaks and tabs (when @p lines).
std::optional<std::string> text_of(const json& v, std::size_t max, bool lines) {
    if (!v.is_string()) {
        return std::nullopt;
    }
    const std::string_view t = trim(v.get_ref<const std::string&>());
    if (t.empty() || chars(t) > max) {
        return std::nullopt;
    }
    for (const char ch : t) {
        const auto c = static_cast<unsigned char>(ch);
        if ((c < 0x20 && !(lines && (c == '\n' || c == '\t' || c == '\r'))) || c == 0x7F) {
            return std::nullopt;
        }
    }
    return std::string(t);
}

Error bad(std::string code, std::string message) {
    return Error{400, std::move(code), std::move(message)};
}

template <std::size_t N>
bool one_of(const std::array<std::string_view, N>& list, const std::string& v) {
    return std::find(list.begin(), list.end(), v) != list.end();
}

std::variant<Item, Error> event_of(const json& body) {
    json payload = json::object();
    for (const char* key : {"source", "title"}) {
        if (const auto it = body.find(key); it != body.end()) {
            const auto label = text_of(*it, kLabelMax, false);
            if (!label) {
                return bad("invalid_" + std::string(key), std::string(key) + " is 1 to 60 characters");
            }
            payload[key] = *label;
        }
    }
    const auto text = text_of(body.value("text", json()), kEventTextMax, true);
    if (!text) {
        return bad("invalid_text", "an event's text is 1 to 2000 characters");
    }
    payload["text"] = *text;
    return Item{"event", std::move(payload), std::nullopt, kTtlDefault};
}

std::variant<Item, Error> item_of(const json& body) {
    const std::string type = body.value("type", json()).is_string() ? body["type"].get<std::string>() : "";
    if (type == "say") {
        const json& t = body.value("text", json());
        if (!t.is_string() || !Now::Voice::line_ok(t.get<std::string>())) {
            return bad("invalid_text", "a say item's text is one line of 1 to 200 characters");
        }
        return Item{"say", json{{"text", std::string(trim(t.get<std::string>()))}}, std::nullopt, kTtlDefault};
    }
    if (type == "outfit") {
        const json& o = body.value("outfit", json());
        if (!o.is_string() || !one_of(kOutfits, o.get<std::string>())) {
            return Error{422, "unknown_outfit", "outfit is p1, p2, lingerie or bare"};
        }
        return Item{"outfit", json{{"outfit", o}}, std::nullopt, kTtlDefault};
    }
    if (type == "act") {
        const json& n = body.value("name", json());
        if (!n.is_string() || !one_of(kActs, n.get<std::string>())) {
            return Error{422, "unknown_act", "name is one of her moves"};
        }
        return Item{"act", json{{"name", n}}, std::nullopt, kTtlDefault};
    }
    if (type == "event") {
        return event_of(body);
    }
    return bad("invalid_type", "type is say, outfit, act or event");
}

}  // namespace

std::variant<Item, Error> parse(std::string_view body, std::string_view content_type) {
    if (body.size() > kBodyMax) {
        return Error{413, "body_too_large", "the body is at most 8 KB"};
    }
    const std::string_view media = trim(content_type.substr(0, content_type.find(';')));
    if (media == "text/plain") {
        json wrapped{{"text", std::string(body)}};
        try {
            (void)wrapped.dump();  // throws on text that is not UTF-8
        } catch (const json::exception&) {
            return bad("invalid_text", "the text is not UTF-8");
        }
        return event_of(wrapped);
    }
    if (media != "application/json" && !media.empty()) {
        return Error{415, "unsupported_media_type", "send application/json or text/plain"};
    }
    const json parsed = json::parse(body, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return bad("invalid_body", "the body is a JSON object");
    }
    auto r = item_of(parsed);
    if (std::holds_alternative<Error>(r)) {
        return r;
    }
    Item& item = std::get<Item>(r);
    if (const auto it = parsed.find("id"); it != parsed.end()) {
        const std::string id = it->is_number_integer() ? std::to_string(it->get<long long>())
                               : it->is_string()       ? it->get<std::string>()
                                                       : "";
        if (id.empty() || chars(id) > kIdMax) {
            return bad("invalid_id", "id is 1 to 200 characters");
        }
        item.source_id = id;
    }
    if (const auto it = parsed.find("ttl"); it != parsed.end()) {
        if (!it->is_number_integer() || it->get<long long>() < 1) {
            return bad("invalid_ttl", "ttl is a whole number of seconds");
        }
        item.ttl = static_cast<int>(std::min<long long>(it->get<long long>(), kTtlMax));
    }
    return r;
}

bool is_silent(std::string_view reply) {
    // Spaces and ASCII punctuation, and the quote marks a model may add (« » “ ”).
    std::string word;
    for (std::size_t i = 0; i < reply.size(); ++i) {
        const auto c = static_cast<unsigned char>(reply[i]);
        if (std::isalpha(c)) {
            word += static_cast<char>(std::toupper(c));
        } else if (c >= 0x80) {
            // A letter outside ASCII is not part of SILENT; the quotes are punctuation.
            const std::string_view rest = reply.substr(i);
            bool quote = false;
            for (const std::string_view q : {"«", "»", "“", "”", "„"}) {
                if (rest.substr(0, q.size()) == q) {
                    i += q.size() - 1;
                    quote = true;
                    break;
                }
            }
            if (!quote) {
                return false;
            }
        } else if (std::isspace(c) == 0 && std::ispunct(c) == 0) {
            return false;
        } else if (!word.empty() && i + 1 < reply.size() && std::isalpha(static_cast<unsigned char>(reply[i + 1]))) {
            return false;  // two words
        }
    }
    return word == "SILENT";
}

std::string redact(std::string_view path) {
    constexpr std::string_view kPrefix = "/api/v1/ani/hook/";
    if (path.substr(0, kPrefix.size()) != kPrefix) {
        return std::string(path);
    }
    return std::string(path.substr(0, kPrefix.size() + 10));
}

}  // namespace Agent::Hook
