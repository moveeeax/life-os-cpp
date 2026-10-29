/**
 * @file TelegramPoll.cpp
 * @brief Body for src/agent/TelegramPoll.hpp.
 */

#include "agent/TelegramPoll.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <variant>

#include <spdlog/spdlog.h>

#include "agent/Hook.hpp"
#include "agent/Intake.hpp"
#include "repositories/telegram/LinkRepository.hpp"
#include "telegram/Gateway.hpp"

namespace Agent::TelegramPoll {

namespace {

using json = nlohmann::json;
using Repositories::Telegram::LinkRepository;

bool word_char(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

/// At most @p n code points of @p s, an ellipsis when cut.
std::string cut(const std::string& s, std::size_t n) {
    std::size_t points = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (points == n) {
                return s.substr(0, i) + "…";
            }
            ++points;
        }
    }
    return s;
}

long long id_of(const json& v) {
    try {
        if (v.is_string()) {
            return std::stoll(v.get<std::string>());
        }
        if (v.is_number_integer()) {
            return v.get<long long>();
        }
    } catch (const std::exception&) {}
    return 0;
}

long long now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

json list_of(const ::Telegram::Reply& r) {
    const json d = r.data();
    return d.is_array() ? d : json::array();
}

/// The newest message id of a list (newest first, but not trusted to be).
long long newest(const json& messages) {
    long long top = 0;
    for (const auto& m : messages) {
        top = std::max(top, id_of(m.value("id", json())));
    }
    return top;
}

}  // namespace

bool mentions(const std::string& text, const std::string& username) {
    if (username.empty() || text.size() <= username.size()) {
        return false;
    }
    for (std::size_t at = text.find('@'); at != std::string::npos; at = text.find('@', at + 1)) {
        if (at > 0 && word_char(static_cast<unsigned char>(text[at - 1]))) {
            continue;
        }
        const std::size_t end = at + 1 + username.size();
        if (end > text.size()) {
            break;
        }
        bool same = true;
        for (std::size_t i = 0; i < username.size() && same; ++i) {
            same = lower(text[at + 1 + i]) == lower(username[i]);
        }
        if (same && (end == text.size() || !word_char(static_cast<unsigned char>(text[end])))) {
            return true;
        }
    }
    return false;
}

std::vector<Incoming> addressed(
    const json& chat, const json& messages, long long cursor, const std::string& username, long long since) {
    std::vector<Incoming> out;
    const std::string type = chat.value("type", "");
    const bool dm = type == "private";
    if (!dm && type != "group" && type != "supergroup") {
        return out;
    }
    for (const auto& m : messages.is_array() ? messages : json::array()) {
        const long long id = id_of(m.value("id", json()));
        const long long date = m.value("date", 0LL);
        if (id <= cursor || m.value("is_outgoing", false) || (cursor == 0 && date < since)) {
            continue;
        }
        const std::string text = ::Telegram::text_of(m);
        if (!dm && !mentions(text, username)) {
            continue;
        }
        out.push_back(Incoming{chat.value("id", std::string()),
                               chat.value("title", std::string()),
                               id,
                               date,
                               dm ? "dm" : "mention",
                               text});
    }
    // Oldest first, as they were written.
    std::sort(out.begin(), out.end(), [](const Incoming& a, const Incoming& b) { return a.id < b.id; });
    return out;
}

long long waiting_since(const json& chat, const json& newest, long long now) {
    if (chat.value("type", "") != "private" || !newest.is_object() || newest.value("is_outgoing", true)) {
        return 0;
    }
    const long long date = newest.value("date", 0LL);
    return date > 0 && date < now - 86400 ? date : 0;
}

std::string event_text(const std::vector<Incoming>& all) {
    std::string out;
    const std::size_t shown = std::min<std::size_t>(all.size(), kMessagesPerEvent);
    for (std::size_t i = 0; i < shown; ++i) {
        const auto& m = all[i];
        std::string line = (m.chat_title.empty() ? "A chat" : m.chat_title) +
                           (m.kind == "mention" ? " (mentions you): " : ": ") +
                           (m.text.empty() ? "[a message]" : cut(m.text, 300));
        out += (out.empty() ? "" : "\n") + line;
    }
    if (all.size() > shown) {
        out += "\nand " + std::to_string(all.size() - shown) + " more";
    }
    return cut(out, 1990);
}

json process_poll(const json&) {
    const auto s = ::Telegram::settings();
    if (!s) {
        return json{{"status", "skipped"}, {"reason", "not_configured"}};
    }
    LinkRepository links;
    const auto link = links.current();
    if (!link) {
        return json{{"status", "skipped"}, {"reason", "not_linked"}};
    }
    auto& transport = Net::Http::transport();
    const auto chats = ::Telegram::call(transport, *s, "GET", "/v1/chats", {{"limit", "100"}});
    if (!chats.ok()) {
        links.failed(link->owner, "chats: http " + std::to_string(chats.status));
        return json{{"status", "failed"}, {"error", "chats"}};
    }
    const auto cursors = links.cursors(link->owner);
    const auto quiet = links.quiet(link->owner);
    const long long since = now_unix() - kNewChatSeconds;
    std::vector<Incoming> found;
    int read = 0;
    for (const auto& chat : list_of(chats)) {
        const std::string id = chat.value("id", std::string());
        const std::string type = chat.value("type", "");
        if (!::Telegram::valid_id(id) || (type != "private" && type != "group" && type != "supergroup")) {
            continue;
        }
        const auto known = cursors.find(id);
        // The first poll marks every chat; later only those with something unread are read.
        if (link->primed && chat.value("unread_count", 0) <= 0) {
            continue;
        }
        if (++read > kChatsPerPoll) {
            break;
        }
        const auto history =
            ::Telegram::call(transport,
                             *s,
                             "GET",
                             "/v1/chats/" + id + "/messages",
                             {{"limit", link->primed ? std::to_string(kMessagesPerChat) : std::string("1")}});
        if (!history.ok()) {
            continue;
        }
        const json messages = list_of(history);
        // A quiet chat moves its cursor but tells nothing.
        if (link->primed && quiet.count(id) == 0) {
            const auto mine =
                addressed(chat, messages, known == cursors.end() ? 0 : known->second, link->username, since);
            found.insert(found.end(), mine.begin(), mine.end());
        }
        if (const long long top = newest(messages); top > 0) {
            links.set_cursor(link->owner, id, top);
        }
    }
    // Once an hour: the private chats whose newest message waits for the owner.
    if (link->primed && !link->waiting_fresh) {
        json waiting = json::array();
        int looked = 0;
        const long long now = now_unix();
        for (const auto& chat : list_of(chats)) {
            const std::string id = chat.value("id", std::string());
            if (chat.value("type", "") != "private" || !::Telegram::valid_id(id) || quiet.count(id) > 0) {
                continue;
            }
            if (++looked > kChatsPerPoll) {
                break;
            }
            const auto top = ::Telegram::call(transport, *s, "GET", "/v1/chats/" + id + "/messages", {{"limit", "1"}});
            const json list = top.ok() ? list_of(top) : json::array();
            if (const long long since = list.empty() ? 0 : waiting_since(chat, list[0], now); since > 0) {
                waiting.push_back({{"chat", id}, {"title", chat.value("title", std::string())}, {"since", since}});
            }
        }
        links.set_waiting(link->owner, waiting);
    }
    links.polled(link->owner);
    if (found.empty()) {
        return json{{"status", "done"}, {"new", 0}};
    }
    std::sort(found.begin(), found.end(), [](const Incoming& a, const Incoming& b) { return a.date < b.date; });
    // The newest message names the event, so a poll run twice does not tell it twice.
    const auto& last = found.back();
    Hook::Item item{"event",
                    json{{"source", "telegram"},
                         {"title", std::to_string(found.size()) + (found.size() == 1 ? " message" : " messages")},
                         {"text", event_text(found)}},
                    "tg:" + last.chat_id + ":" + std::to_string(last.id),
                    Hook::kTtlDefault};
    try {
        const auto out = Intake::accept(link->owner, std::move(item), "telegram");
        if (const auto* refused = std::get_if<Intake::Refused>(&out)) {
            spdlog::info("telegram poll: Ani did not take the event: {}", refused->code);
            return json{{"status", "done"}, {"new", found.size()}, {"event", refused->code}};
        }
    } catch (const std::exception& e) {
        spdlog::warn("telegram poll: the event failed: {}", e.what());
    }
    return json{{"status", "done"}, {"new", found.size()}};
}

}  // namespace Agent::TelegramPoll
