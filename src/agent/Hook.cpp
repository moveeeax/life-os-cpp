/**
 * @file Hook.cpp
 * @brief Bodies for src/agent/Hook.hpp.
 */

#include "agent/Hook.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
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

/// A whole number in [lo, hi] at @p key; @p fallback when absent; nullopt when wrong.
std::optional<long long> int_in(
    const json& body, const char* key, long long lo, long long hi, std::optional<long long> fallback = std::nullopt) {
    const auto it = body.find(key);
    if (it == body.end()) {
        return fallback;
    }
    if (!it->is_number_integer() || it->get<long long>() < lo || it->get<long long>() > hi) {
        return std::nullopt;
    }
    return it->get<long long>();
}

bool is_uuid(const std::string& s) {
    static const std::regex re("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
    return std::regex_match(s, re);
}

/// An https URL with a host, up to 2000 characters, no spaces or controls.
bool is_https(const json& v) {
    if (!v.is_string()) {
        return false;
    }
    const std::string& u = v.get_ref<const std::string&>();
    constexpr std::string_view head = "https://";
    if (u.size() > 2000 || u.substr(0, head.size()) != head) {
        return false;
    }
    if (std::any_of(u.begin(), u.end(), [](char c) { return static_cast<unsigned char>(c) <= 0x20 || c == 0x7F; })) {
        return false;
    }
    const auto end = u.find_first_of("/?#", head.size());
    return (end == std::string::npos ? u.size() : end) > head.size();
}

bool is_iso_time(const std::string& s) {
    static const std::regex re(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}(:\d{2}(\.\d{1,6})?)?(Z|[+-]\d{2}:\d{2})$)");
    return std::regex_match(s, re);
}

/// One spoken line (say, remind, ask).
std::optional<std::string> line_of(const json& body) {
    const json& t = body.value("text", json());
    if (!t.is_string() || !Now::Voice::line_ok(t.get<std::string>())) {
        return std::nullopt;
    }
    return std::string(trim(t.get<std::string>()));
}

std::variant<Item, Error> more_of(const std::string& type, const json& body) {
    if (type == "mood") {
        const auto level = int_in(body, "level", -1, 1);
        if (!level || !body.contains("level")) {
            return bad("invalid_level", "level is -1, 0 or 1");
        }
        const auto seconds = int_in(body, "seconds", 60, 3600, kMoodSecondsDefault);
        if (!seconds) {
            return bad("invalid_seconds", "seconds is 60 to 3600");
        }
        return Item{"mood", json{{"level", *level}, {"seconds", *seconds}}, std::nullopt, kTtlDefault};
    }
    if (type == "remind") {
        const auto text = line_of(body);
        if (!text) {
            return bad("invalid_text", "a reminder's text is one line of 1 to 200 characters");
        }
        const bool has_in = body.contains("in_seconds");
        const bool has_at = body.contains("at");
        Item item{"remind", json{{"text", *text}}, std::nullopt, kTtlDefault};
        if (has_in == has_at) {
            return bad("invalid_when", "give in_seconds (60 to 604800) or at (ISO-8601), one of them");
        }
        if (has_in) {
            const auto in = int_in(body, "in_seconds", kRemindMinSeconds, kRemindMaxSeconds);
            if (!in) {
                return bad("invalid_when", "in_seconds is 60 to 604800");
            }
            item.delay_seconds = *in;
        } else {
            const json& at = body["at"];
            if (!at.is_string() || !is_iso_time(at.get<std::string>())) {
                return bad("invalid_when", "at is an ISO-8601 time with a zone, like 2026-10-28T18:30:00+05:00");
            }
            item.at = at.get<std::string>();
        }
        return item;
    }
    if (type == "ask") {
        const auto text = line_of(body);
        if (!text) {
            return bad("invalid_text", "a question is one line of 1 to 200 characters");
        }
        json options = json::array({"Да", "Нет"});
        if (const auto it = body.find("options"); it != body.end()) {
            if (!it->is_array() || it->empty() || it->size() > 4) {
                return bad("invalid_options", "options are 1 to 4 answers of 1 to 30 characters");
            }
            options = json::array();
            for (const auto& o : *it) {
                const auto label = text_of(o, 30, false);
                if (!label) {
                    return bad("invalid_options", "options are 1 to 4 answers of 1 to 30 characters");
                }
                options.push_back(*label);
            }
        }
        Item item{"ask", json{{"text", *text}, {"options", options}}, std::nullopt, kAskTtlDefault};
        if (const auto it = body.find("callback"); it != body.end()) {
            if (!is_https(*it)) {
                return bad("invalid_callback", "callback is an https URL");
            }
            item.callback = it->get<std::string>();
        }
        return item;
    }
    if (type == "focus") {
        const std::string action = body.value("action", json()).is_string() ? body["action"].get<std::string>() : "";
        if (action == "stop") {
            return Item{"focus", json{{"action", "stop"}}, std::nullopt, kTtlDefault};
        }
        if (action != "start") {
            return bad("invalid_action", "action is start or stop");
        }
        const auto minutes = int_in(body, "minutes", 1, kFocusMaxMinutes);
        if (!minutes || !body.contains("minutes")) {
            return bad("invalid_minutes", "minutes is 1 to 180");
        }
        return Item{"focus", json{{"action", "start"}, {"minutes", *minutes}}, std::nullopt, kTtlDefault};
    }
    if (type == "card") {
        const auto title = text_of(body.value("title", json()), kLabelMax, false);
        if (!title) {
            return bad("invalid_title", "title is 1 to 60 characters");
        }
        json payload{{"title", *title}};
        if (const auto it = body.find("text");
            it != body.end() && !(it->is_string() && it->get<std::string>().empty())) {
            const auto text = text_of(*it, 200, true);
            if (!text) {
                return bad("invalid_text", "text is up to 200 characters");
            }
            payload["text"] = *text;
        }
        if (const auto it = body.find("url"); it != body.end()) {
            if (!is_https(*it)) {
                return bad("invalid_url", "url is an https URL");
            }
            payload["url"] = *it;
        }
        const auto seconds = int_in(body, "seconds", 5, 120, kCardSecondsDefault);
        if (!seconds) {
            return bad("invalid_seconds", "seconds is 5 to 120");
        }
        payload["seconds"] = *seconds;
        return Item{"card", std::move(payload), std::nullopt, kTtlDefault};
    }
    if (type == "wallpaper" || type == "sound") {
        json payload = json::object();
        if (const auto it = body.find("which"); it != body.end()) {
            const bool off = type == "sound" && it->is_string() && it->get<std::string>() == "off";
            if (!off && !(it->is_string() && is_uuid(it->get<std::string>()))) {
                return bad("invalid_which",
                           type == "sound" ? "which is a sound's id or off" : "which is a wallpaper's id");
            }
            payload["which"] = *it;
        }
        return Item{type, std::move(payload), std::nullopt, kTtlDefault};
    }
    if (type == "hold") {
        const auto minutes = int_in(body, "minutes", 0, kHoldMaxMinutes);
        if (!minutes || !body.contains("minutes")) {
            return bad("invalid_minutes", "minutes is 0 to 480");
        }
        return Item{"hold", json{{"minutes", *minutes}}, std::nullopt, kTtlDefault};
    }
    return bad("invalid_type",
               "type is say, outfit, act, event, mood, remind, ask, focus, card, wallpaper, sound or hold");
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
    return more_of(type, body);
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

std::string defanged(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '[') {
            out += '(';
        } else if (c == ']') {
            out += ')';
        } else if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            out += " / ";
        } else {
            out += c;
        }
    }
    return out;
}

}  // namespace Agent::Hook
