/**
 * @file TaskActions.cpp
 * @brief The write tools on tasks, habits, notes and the diary (agent/ActionKit.hpp).
 */

#include <algorithm>
#include <string>
#include <vector>

#include "agent/ActionKit.hpp"
#include "agent/SiteApi.hpp"

namespace Agent::Actions::detail {

namespace {

const json kAreas = {"finance", "health", "travel", "growth", "relationships", "projects"};

const json kDay = {{"type", "string"}, {"description", "A day as YYYY-MM-DD"}};

}  // namespace

json create_task_tool() {
    return tool(
        "create_task",
        "Propose a new task. Nothing is written until the person confirms it with a button.",
        object({{"title", {{"type", "string"}, {"description", "The task, short, in the person's words"}}},
                {"area", {{"type", "string"}, {"enum", kAreas}, {"description", "The part of life it belongs to"}}},
                {"due", kDay}},
               json::array({"title", "area"})));
}

json complete_task_tool() {
    return tool(
        "complete_task",
        "Propose to close an open task as done. Nothing changes until the person confirms it.",
        object({{"task", {{"type", "string"}, {"description", "Its title, or words of it"}}}}, json::array({"task"})));
}

json check_habit_tool() {
    return tool("check_habit",
                "Propose to tick a habit for a day (today unless given), or add an amount to it. Nothing changes "
                "until the person confirms it.",
                object({{"habit", {{"type", "string"}, {"description", "Its name as the habits tool gives it"}}},
                        {"date", kDay},
                        {"amount", {{"type", "number"}, {"description", "An amount to add, in the habit's unit"}}}},
                       json::array({"habit"})));
}

json add_note_tool() {
    return tool("add_note",
                "Propose a note to the person's inbox, to sort later. Nothing is written until they confirm it.",
                object({{"text", {{"type", "string"}, {"description", "The note"}}}}, json::array({"text"})));
}

json journal_note_tool() {
    return tool("journal_note",
                "Propose today's diary entry, written from what the person told you about their day, in their words "
                "and first person. Nothing is written until they confirm it.",
                object({{"title", {{"type", "string"}, {"description", "A few words for the day"}}},
                        {"body", {{"type", "string"}, {"description", "The entry, a few short paragraphs"}}}},
                       json::array({"title", "body"})));
}

Proposed create_task(const Ctx& c, const json& args) {
    const std::string title = text_arg(args, "title");
    if (title.empty() || length(title) > 200) {
        return refusal("invalid", "the title is 1 to 200 characters");
    }
    const std::string area = text_arg(args, "area");
    if (std::find(kAreas.begin(), kAreas.end(), json(area)) == kAreas.end()) {
        return refusal("invalid", "the area is one of the list", json{{"areas", kAreas}});
    }
    json body = {{"title", title}, {"area", area}};
    std::string summary = "Add the task «" + title + "»";
    if (const std::string due = day_arg(args, "due"); !due.empty()) {
        body["due"] = due;
        summary += ", due " + due;
    }
    return Proposal{"create_task",
                    summary,
                    json{{"method", "POST"},
                         {"path", "/api/v1/tasks/items"},
                         {"query", json::array({json::array({"tz", c.tz})})},
                         {"body", body}}};
}

Proposed complete_task(const Ctx& c, const json& args) {
    const std::string words = text_arg(args, "task");
    if (words.empty()) {
        return refusal("invalid", "say which task: words of its title");
    }
    const auto found = SiteApi::read_data(
        c.transport,
        c.api_url,
        c.token,
        "/api/v1/tasks/items",
        json::array({json::array({"q", cut(words, 200)}), json::array({"limit", "10"}), json::array({"tz", c.tz})}));
    if (!found || !found->is_array()) {
        return refusal("unavailable", "the tasks could not be read");
    }
    std::vector<json> open;
    for (const auto& t : *found) {
        if (t.is_object() && t.value("status", "") != "done") {
            open.push_back(t);
        }
    }
    const json* pick = open.size() == 1 ? &open[0] : nullptr;
    for (const auto& t : open) {
        if (pick == nullptr && t.value("title", "") == words) {
            pick = &t;
        }
    }
    if (pick == nullptr) {
        if (open.empty()) {
            return refusal("not_found", "no open task has these words; look it up with find_tasks");
        }
        json titles = json::array();
        for (const auto& t : open) {
            titles.push_back(t.value("title", ""));
        }
        return refusal(
            "ambiguous", "several open tasks match: ask which, or give its full title", json{{"matches", titles}});
    }
    return Proposal{"complete_task",
                    "Close the task «" + pick->value("title", "") + "»",
                    json{{"method", "PATCH"},
                         {"path", "/api/v1/tasks/items/" + pick->value("id", "")},
                         {"query", json::array({json::array({"tz", c.tz})})},
                         {"body", {{"status", "done"}}}}};
}

Proposed check_habit(const Ctx& c, const json& args) {
    const std::string name = text_arg(args, "habit");
    if (name.empty()) {
        return refusal("invalid", "say which habit: its name as the habits tool gives it");
    }
    const auto habits = SiteApi::read_data(c.transport, c.api_url, c.token, "/api/v1/habits", json::array());
    if (!habits || !habits->is_array()) {
        return refusal("unavailable", "the habits could not be read");
    }
    const json* pick = nullptr;
    std::vector<const json*> near;
    json names = json::array();
    for (const auto& h : *habits) {
        const std::string n = h.value("name", "");
        names.push_back(n);
        if (n == name) {
            pick = &h;
        } else if (n.find(name) != std::string::npos || name.find(n) != std::string::npos) {
            near.push_back(&h);
        }
    }
    if (pick == nullptr && near.size() == 1) {
        pick = near[0];
    }
    if (pick == nullptr) {
        return refusal(near.empty() ? "not_found" : "ambiguous",
                       "give the habit's name exactly as one of these",
                       json{{"habits", names}});
    }
    const std::string source = pick->value("source", "manual");
    if (source != "manual") {
        return refusal("automatic", "this habit is ticked by its source (" + source + "), not by hand");
    }
    const std::string date = day_arg(args, "date").empty() ? c.today : day_arg(args, "date");
    if (date.empty() || date > c.today) {
        return refusal("invalid", "a habit is ticked for today or a past day");
    }
    const std::string habit = pick->value("name", "");
    const std::string path = "/api/v1/habits/" + pick->value("id", "") + "/checks/" + date;
    const std::string day = date == c.today ? "today" : date;
    const json query = json::array({json::array({"tz", c.tz})});
    const auto amount = args.find("amount");
    if (amount != args.end() && amount->is_number() && amount->get<double>() > 0 &&
        amount->get<double>() <= 1'000'000) {
        const double value = amount->get<double>();
        json shown = value == static_cast<double>(static_cast<long long>(value)) ? json(static_cast<long long>(value))
                                                                                 : json(value);
        const std::string unit = pick->value("unit", "");
        return Proposal{
            "check_habit",
            "Add " + shown.dump() + (unit.empty() ? "" : " " + unit) + " to «" + habit + "» for " + day,
            json{{"method", "POST"}, {"path", path + "/add"}, {"query", query}, {"body", {{"value", value}}}}};
    }
    return Proposal{"check_habit",
                    "Tick «" + habit + "» for " + day,
                    json{{"method", "PUT"}, {"path", path}, {"query", query}, {"body", json::object()}}};
}

Proposed add_note(const Ctx& /*c*/, const json& args) {
    const std::string text = text_arg(args, "text");
    if (text.empty() || length(text) > 2000) {
        return refusal("invalid", "the note is 1 to 2000 characters");
    }
    return Proposal{"add_note",
                    "Note to the inbox: «" + cut(text, 200) + "»",
                    json{{"method", "POST"}, {"path", "/api/v1/tasks/notes"}, {"body", {{"text", text}}}}};
}

/// The evening diary she writes from a talk: a note under the topic journal for the person's day.
Proposed journal_note(const Ctx& c, const json& args) {
    const std::string title = text_arg(args, "title");
    const std::string body = text_arg(args, "body");
    if (title.empty() || length(title) > 120) {
        return refusal("invalid", "the title is 1 to 120 characters");
    }
    if (body.empty() || length(body) > 4000) {
        return refusal("invalid", "the entry is 1 to 4000 characters");
    }
    json note{{"title", title}, {"body", body}, {"topic", "journal"}};
    if (c.today.size() == 10) {
        note["date"] = c.today;
    }
    return Proposal{"journal_note",
                    "Diary entry «" + cut(title, 80) + "»: «" + cut(body, 200) + "»",
                    json{{"method", "POST"}, {"path", "/api/v1/notes"}, {"body", note}}};
}

}  // namespace Agent::Actions::detail
