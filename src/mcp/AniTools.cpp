/**
 * @file AniTools.cpp
 * @brief Body for src/mcp/AniTools.hpp.
 */

#include "mcp/AniTools.hpp"

#include <algorithm>
#include <exception>
#include <memory>
#include <set>
#include <utility>
#include <variant>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include "agent/Actions.hpp"
#include "agent/Hook.hpp"
#include "agent/Intake.hpp"
#include "agent/Presence.hpp"
#include "agent/Settings.hpp"
#include "agent/Turn.hpp"
#include "jobs/Jobs.hpp"
#include "repositories/agent/AgentRepository.hpp"
#include "repositories/agent/InboxRepository.hpp"
#include "repositories/agent/PrefsRepository.hpp"
#include "repositories/agent/TurnRepository.hpp"
#include "repositories/now/SoundRepository.hpp"
#include "repositories/now/WallpaperRepository.hpp"

namespace Mcp {

namespace {

using json = nlohmann::json;
using Repositories::Agent::InboxRepository;
using Repositories::Agent::TurnRepository;

/// A short line for the model, then the data.
json result(const std::string& line, const json& data) {
    return tool_text(line + "\n" + data.dump(2));
}

json refusal(const std::string& line) {
    return tool_text(line, true);
}

/// Her moves as a JSON enum.
json acts() {
    json a = json::array();
    for (const auto n : Agent::Hook::kActs) {
        a.push_back(std::string(n));
    }
    return a;
}

json schema(json properties, json required = json::array()) {
    json s{{"type", "object"}, {"properties", std::move(properties)}, {"additionalProperties", false}};
    if (!required.empty()) {
        s["required"] = std::move(required);
    }
    return s;
}

const json kId{{"type", "string"},
               {"maxLength", 200},
               {"description", "Your id for this item; a repeat within 24 hours is refused"}};
const json kTtl{{"type", "integer"},
                {"minimum", 1},
                {"maximum", Agent::Hook::kTtlMax},
                {"description", "Seconds it may wait for the Now page (default a day)"}};

int clamp_arg(const json& args, const char* key, int fallback, int lo, int hi) {
    const auto it = args.find(key);
    const int v = it != args.end() && it->is_number_integer() ? it->get<int>() : fallback;
    return std::clamp(v, lo, hi);
}

std::size_t utf8_length(const std::string& s) {
    std::size_t n = 0;
    for (const unsigned char c : s) {
        n += (c & 0xC0) != 0x80 ? 1 : 0;
    }
    return n;
}

/// say, outfit, act or event: through the webhook's parser and intake.
json command(const std::string& owner, const std::string& type, const json& args) {
    if (!Agent::Intake::within_rate(owner)) {
        return refusal("At most 120 commands an hour (shared with the webhook); try later.");
    }
    json body = args;
    body["type"] = type;
    auto parsed = Agent::Hook::parse(body.dump(), "application/json");
    if (const auto* e = std::get_if<Agent::Hook::Error>(&parsed)) {
        return refusal(e->message.empty() ? e->code : e->message);
    }
    auto got = Agent::Intake::accept(owner, std::get<Agent::Hook::Item>(std::move(parsed)), "mcp");
    if (const auto* r = std::get_if<Agent::Intake::Refused>(&got)) {
        return refusal(r->message);
    }
    const std::string id = std::get<Agent::Intake::Accepted>(got).id;
    if (type == "event") {
        return result("Sent to her agent; she decides whether to say something.",
                      json{{"id", id}, {"status", "thinking"}});
    }
    if (type == "hold") {
        return result("Done.", json{{"id", id}, {"status", "delivered"}});
    }
    return result(type == "ask" ? "Asked on the Now page; read the answer with ani_item." : "Queued for the Now page.",
                  json{{"id", id}, {"status", "ready"}});
}

json presence(const std::string& owner) {
    const auto ago = Agent::Presence::seen_ago(owner);
    const auto held = Repositories::Agent::PrefsRepository().held_until(owner);
    json data{{"page", "unknown"}};
    std::string line = "Not known whether the Now page is open.";
    if (ago) {
        const bool open = *ago <= Agent::Presence::kOpenSeconds;
        data = json{{"page", open ? "open" : "away"}, {"seen_seconds_ago", *ago}};
        line = open ? "The Now page is open." : "The Now page is not open.";
    }
    if (held) {
        data["held_until"] = *held;
        line += " Everything is held back until then.";
    }
    return result(line, data);
}

/// The wallpapers or sounds the page can switch to: [{id, title or kind}].
json choices(const std::string& what) {
    json out = json::array();
    if (what == "wallpaper") {
        for (const auto& w : Repositories::Now::WallpaperRepository().list_ready("desktop")) {
            out.push_back(json{{"id", w.value("id", "")}, {"kind", w.value("kind", "")}});
        }
    } else {
        for (const auto& x : Repositories::Now::SoundRepository().list_ready()) {
            out.push_back(json{{"id", x.value("id", "")}, {"title", x.value("title", "")}});
        }
    }
    return out;
}

/// A turn as the MCP shows it; "starting" while it waits for her agent to come up.
json turn_view(const std::string& owner, const json& t) {
    json v{{"turn_id", t["id"]},
           {"status", t["status"]},
           {"origin", t["origin"]},
           {"text", t["text"]},
           {"reply", t["reply"]},
           {"created_at", t["created_at"]}};
    if (t.contains("error") && !t["error"].is_null()) {
        v["error"] = t["error"];
    }
    if (t.value("status", "") == "queued") {
        const auto agent = Repositories::Agent::AgentRepository().get(owner);
        const std::string state = agent ? agent->value("state", "stopped") : "stopped";
        if (state != "running") {
            v["status"] = "starting";
        }
    }
    json waiting = json::array();
    if (t.contains("actions") && t["actions"].is_array()) {
        for (const auto& a : t["actions"]) {
            if (a.value("status", "") == "pending") {
                waiting.push_back(a.value("summary", ""));
            }
        }
    }
    if (!waiting.empty()) {
        v["waiting_for_confirmation_on_now"] = waiting;
    }
    return v;
}

json chat(const std::string& owner, const json& args) {
    if (!Agent::enabled()) {
        return refusal("Her agent is off.");
    }
    const std::string text = args.value("text", "");
    if (text.find_first_not_of(" \t\r\n") == std::string::npos || utf8_length(text) > Agent::kTurnTextMax) {
        return refusal("text is 1 to " + std::to_string(Agent::kTurnTextMax) + " characters.");
    }
    if (!Jobs::is_initialized()) {
        return refusal("The job queue is not enabled.");
    }
    TurnRepository turns;
    if (turns.count_since(owner, 3600) >= Agent::kTurnsPerHour) {
        return refusal("At most " + std::to_string(Agent::kTurnsPerHour) + " messages an hour; try later.");
    }
    auto sent = Agent::send(owner, text, turns.last_tz(owner), "mcp");
    switch (sent.status) {
        case Agent::Send::Queued:
            return result("Sent. Ask ani_reply with this turn_id for her answer.", turn_view(owner, sent.turn));
        case Agent::Send::Open:
            return refusal("She is still answering your last MCP message; wait for it with ani_reply.");
        case Agent::Send::Busy:
            return refusal("Every agent slot is taken, try again later.");
        case Agent::Send::Budget:
            return refusal("This month's tokens are spent.");
        case Agent::Send::Off:
            return refusal("Her agent is not configured.");
    }
    return refusal("Her agent is not configured.");
}

/// The MCP's proposal tools and the write tools of her agent they stand for.
const std::vector<std::pair<std::string, std::string>> kProposals = {
    {"ani_task", "create_task"}, {"ani_habit", "check_habit"}, {"ani_note", "add_note"}};

/// The input schema of one of her agent's write tools.
json write_schema(const std::string& tool) {
    for (const auto& t : Agent::Actions::definitions()) {
        if (t["function"].value("name", "") == tool) {
            return t["function"]["parameters"];
        }
    }
    return schema(json::object());
}

json propose(const std::string& owner, const std::string& tool, const json& args) {
    if (!Agent::enabled()) {
        return refusal("Her agent is off.");
    }
    if (!Jobs::is_initialized()) {
        return refusal("The job queue is not enabled.");
    }
    TurnRepository turns;
    if (turns.count_since(owner, 3600) >= Agent::kTurnsPerHour) {
        return refusal("At most " + std::to_string(Agent::kTurnsPerHour) + " messages an hour; try later.");
    }
    auto sent = Agent::propose(owner, tool, args);
    switch (sent.status) {
        case Agent::Send::Queued:
            return result(
                "Proposed. The user confirms it on Now; ani_reply with this turn_id shows the proposal "
                "and later whether it was done.",
                turn_view(owner, sent.turn));
        case Agent::Send::Open:
            return refusal("Your last MCP message is still open; wait for it with ani_reply.");
        default:
            return refusal("Proposals need the site's API, which is not configured.");
    }
}

void reply(const std::string& owner, const json& args, const Scheduler& after, Reply done) {
    const std::string id = args.value("turn_id", "");
    const int wait = clamp_arg(args, "wait_seconds", 0, 0, kReplyWaitMax);
    // Checks the turn, then again every second until it ends or the wait is
    // over. The closure keeps itself only weakly; each pending timer holds
    // the strong reference, so it goes once the answer is given.
    auto check = std::make_shared<std::function<void(int)>>();
    std::weak_ptr<std::function<void(int)>> self = check;
    *check = [owner, id, wait, after, done, self](int n) {
        try {
            const auto t = TurnRepository().get(owner, id);
            if (!t) {
                return done(refusal("No such turn."));
            }
            const std::string status = t->value("status", "");
            if (status == "done" || status == "failed" || n >= wait) {
                const json v = turn_view(owner, *t);
                const std::string shown = v.value("status", status);
                return done(result(status == "done"     ? "Her answer."
                                   : status == "failed" ? "The turn failed."
                                                        : "No answer yet (" + shown + "); ask again later.",
                                   v));
            }
            if (auto next = self.lock()) {
                after(1.0, [next, n] { (*next)(n + 1); });
            }
        } catch (const std::exception& e) {
            spdlog::error("ani.mcp reply failed: {}", e.what());
            done(refusal("Could not read the turn."));
        }
    };
    (*check)(0);
}

}  // namespace

Scheduler loop_scheduler() {
    return [](double seconds, std::function<void()> fn) { drogon::app().getLoop()->runAfter(seconds, std::move(fn)); };
}

Server ani_server(const std::string& owner, Scheduler after) {
    Server s;
    s.name = "life-os-ani";
    s.version = "1";
    s.instructions =
        "Ani lives on the user's Now page in Life OS. Commands wait for the page and play when it is open; "
        "ani_presence says whether it is. ani_chat talks with her agent whether or not the page is open.";
    s.tools = {
        {"ani_say",
         "Ani says a line aloud on the Now page (1-200 characters).",
         schema({{"text", {{"type", "string"}, {"minLength", 1}, {"maxLength", 200}}}, {"id", kId}, {"ttl", kTtl}},
                {"text"})},
        {"ani_outfit",
         "Ani changes her outfit (bare only while the user turned it on).",
         schema(
             {{"outfit", {{"type", "string"}, {"enum", {"p1", "p2", "lingerie", "bare"}}}}, {"id", kId}, {"ttl", kTtl}},
             {"outfit"})},
        {"ani_act",
         "Ani plays one of her moves.",
         schema({{"name", {{"type", "string"}, {"enum", acts()}}}, {"id", kId}, {"ttl", kTtl}}, {"name"})},
        {"ani_event",
         "An event for her agent, which decides whether to tell the user or stay silent.",
         schema({{"text", {{"type", "string"}, {"minLength", 1}, {"maxLength", 2000}}},
                 {"source", {{"type", "string"}, {"maxLength", 60}}},
                 {"title", {{"type", "string"}, {"maxLength", 60}}},
                 {"id", kId},
                 {"ttl", kTtl}},
                {"text"})},
        {"ani_presence", "Whether the Now page is open now.", schema(json::object())},
        {"ani_items",
         "The latest items sent to her (MCP and webhook) and what became of them.",
         schema({{"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", kListMax}}}})},
        {"ani_item", "One item sent to her.", schema({{"id", {{"type", "string"}}}}, {"id"})},
        {"ani_chat",
         "A message to her agent, as in the chat on Now; returns a turn_id at once.",
         schema({{"text", {{"type", "string"}, {"minLength", 1}, {"maxLength", Agent::kTurnTextMax}}}}, {"text"})},
        {"ani_reply",
         "Her answer to a chat turn; wait_seconds (up to 25) waits for it.",
         schema({{"turn_id", {{"type", "string"}}},
                 {"wait_seconds", {{"type", "integer"}, {"minimum", 0}, {"maximum", kReplyWaitMax}}}},
                {"turn_id"})},
        {"ani_mood",
         "Her mood shows in her face for a while: -1 a little cross, 0 as usual, 1 happy.",
         schema({{"level", {{"type", "integer"}, {"minimum", -1}, {"maximum", 1}}},
                 {"seconds", {{"type", "integer"}, {"minimum", 60}, {"maximum", 3600}}},
                 {"id", kId},
                 {"ttl", kTtl}},
                {"level"})},
        {"ani_remind",
         "She says a line later: in_seconds (60 to a week) or at (ISO-8601 with a zone), one of them.",
         schema({{"text", {{"type", "string"}, {"minLength", 1}, {"maxLength", 200}}},
                 {"in_seconds", {{"type", "integer"}, {"minimum", 60}, {"maximum", 604800}}},
                 {"at", {{"type", "string"}, {"description", "e.g. 2026-10-28T18:30:00+05:00"}}},
                 {"id", kId},
                 {"ttl", kTtl}},
                {"text"})},
        {"ani_ask",
         "She asks the user with answer buttons on Now (default Да / Нет); read the answer with ani_item, or "
         "give an https callback to get it POSTed.",
         schema({{"text", {{"type", "string"}, {"minLength", 1}, {"maxLength", 200}}},
                 {"options",
                  {{"type", "array"},
                   {"minItems", 1},
                   {"maxItems", 4},
                   {"items", {{"type", "string"}, {"minLength", 1}, {"maxLength", 30}}}}},
                 {"callback", {{"type", "string"}, {"description", "https URL"}}},
                 {"id", kId},
                 {"ttl", kTtl}},
                {"text"})},
        {"ani_focus",
         "Starts (minutes 1-180) or stops the focus timer on Now.",
         schema({{"action", {{"type", "string"}, {"enum", {"start", "stop"}}}},
                 {"minutes", {{"type", "integer"}, {"minimum", 1}, {"maximum", 180}}},
                 {"id", kId},
                 {"ttl", kTtl}},
                {"action"})},
        {"ani_card",
         "A small card above her ledge for a few seconds, not spoken; a url opens on click.",
         schema({{"title", {{"type", "string"}, {"minLength", 1}, {"maxLength", 60}}},
                 {"text", {{"type", "string"}, {"maxLength", 200}}},
                 {"url", {{"type", "string"}, {"description", "https URL"}}},
                 {"seconds", {{"type", "integer"}, {"minimum", 5}, {"maximum", 120}}},
                 {"id", kId},
                 {"ttl", kTtl}},
                {"title"})},
        {"ani_wallpaper",
         "Switches the wallpaper of Now (which: an id; none: the next one); list: true returns the ids.",
         schema({{"which", {{"type", "string"}}}, {"list", {{"type", "boolean"}}}, {"id", kId}, {"ttl", kTtl}})},
        {"ani_sound",
         "Switches the focus sound (which: an id, or off); list: true returns the ids and titles.",
         schema({{"which", {{"type", "string"}}}, {"list", {{"type", "boolean"}}}, {"id", kId}, {"ttl", kTtl}})},
        {"ani_hold",
         "Holds everything back from the page for some minutes (0 to 480; 0 ends a hold).",
         schema({{"minutes", {{"type", "integer"}, {"minimum", 0}, {"maximum", 480}}}, {"id", kId}}, {"minutes"})},
        {"ani_task",
         "Proposes a new task; the user confirms it on Now before anything is written.",
         write_schema("create_task")},
        {"ani_habit",
         "Proposes to tick a habit (today unless a date is given) or add an amount; confirmed by the user on Now.",
         write_schema("check_habit")},
        {"ani_note", "Proposes a note to the user's inbox; confirmed by the user on Now.", write_schema("add_note")},
        {"ani_history",
         "The latest messages of her chat, from every source.",
         schema({{"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", kListMax}}}})},
    };
    s.run = [owner, after](const std::string& name, const json& args, Reply done) {
        try {
            if ((name == "ani_wallpaper" || name == "ani_sound") && args.value("list", false) == true) {
                return done(result("The ids to switch to.", choices(name.substr(4))));
            }
            static const std::set<std::string> commands = {"ani_say",
                                                           "ani_outfit",
                                                           "ani_act",
                                                           "ani_event",
                                                           "ani_mood",
                                                           "ani_remind",
                                                           "ani_ask",
                                                           "ani_focus",
                                                           "ani_card",
                                                           "ani_wallpaper",
                                                           "ani_sound",
                                                           "ani_hold"};
            if (commands.count(name)) {
                json body = args;
                body.erase("list");
                return done(command(owner, name.substr(4), body));
            }
            if (name == "ani_presence") {
                return done(presence(owner));
            }
            if (name == "ani_items") {
                const json items = InboxRepository().items(owner, clamp_arg(args, "limit", kItemsDefault, 1, kListMax));
                return done(result("Newest first.", items));
            }
            if (name == "ani_item") {
                const auto item = InboxRepository().item(owner, args.value("id", ""));
                return done(item ? result("The item.", *item) : refusal("No such item."));
            }
            if (name == "ani_chat") {
                return done(chat(owner, args));
            }
            if (name == "ani_reply") {
                return reply(owner, args, after, std::move(done));
            }
            for (const auto& [mcp_name, tool] : kProposals) {
                if (name == mcp_name) {
                    return done(propose(owner, tool, args));
                }
            }
            if (name == "ani_history") {
                json out = json::array();
                for (const auto& t :
                     TurnRepository().list(owner, "", clamp_arg(args, "limit", kHistoryDefault, 1, kListMax))) {
                    out.push_back(turn_view(owner, t));
                }
                return done(result("Newest first.", out));
            }
            done(refusal("Unknown tool."));
        } catch (const std::exception& e) {
            spdlog::error("ani.mcp {} failed: {}", name, e.what());
            done(refusal("Something went wrong on the server."));
        }
    };
    return s;
}

}  // namespace Mcp
