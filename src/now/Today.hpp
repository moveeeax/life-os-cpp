/**
 * @file Today.hpp
 * @brief The day's digest of the Now screen (GET /api/v1/today): each
 *        section is a part that may be off, empty or failing on its own, and
 *        the blocks are cut from what the sections already compute. No rule
 *        of its own beyond ordering; the words are the frontend's.
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <exception>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "tasks/Agenda.hpp"
#include "utils/Date.hpp"

namespace Now::Today {

using json = nlohmann::json;

/// How many late tasks and spends the corners list.
inline constexpr std::size_t kLateShown = 5;
inline constexpr std::size_t kSpendsShown = 5;
/// The weight change compares with the latest weighing at least this many days older.
inline constexpr int kWeightChangeDays = 6;

struct Part {
    std::string key;             // "tasks" | "goals" | "food" | "money" | "health"
    bool enabled = false;        // the section's module switch
    std::function<json()> load;  // null json: nothing to show
};

/// Runs each enabled part; a part that throws lands in "errors", never fails the whole.
inline json assemble(const std::vector<Part>& parts, json focus) {
    json out = json::object();
    json errors = json::array();
    for (const auto& p : parts) {
        if (!p.enabled) {
            continue;
        }
        try {
            json block = p.load();
            if (!block.is_null()) {
                out[p.key] = std::move(block);
            }
        } catch (const std::exception& e) {
            spdlog::warn("today: the {} part failed: {}", p.key, e.what());
            errors.push_back(p.key);
        }
    }
    out["focus"] = std::move(focus);
    out["errors"] = std::move(errors);
    return out;
}

/// {late_count, first_late: {id, title, due} | null, late: up to 5 {id, title, due} oldest first,
/// today: [{id, title, status}], inbox_count}.
inline json tasks_block(const ::Tasks::Agenda::Groups& g, std::string_view date, long inbox_count) {
    json first_late;
    for (const auto& row : g.late) {
        if (first_late.is_null() || row.value("due", "") < first_late["due"].get<std::string>()) {
            first_late = json{{"id", row["id"]}, {"title", row["title"]}, {"due", row.value("due", "")}};
        }
    }
    json today = json::array();
    for (const auto& row : g.soon) {
        if (row.value("due", "") == date) {
            today.push_back(json{{"id", row["id"]}, {"title", row["title"]}, {"status", row.value("status", "open")}});
        }
    }
    std::vector<json> late(g.late.begin(), g.late.end());
    std::stable_sort(
        late.begin(), late.end(), [](const json& a, const json& b) { return a.value("due", "") < b.value("due", ""); });
    json late_out = json::array();
    for (std::size_t i = 0; i < late.size() && i < kLateShown; ++i) {
        late_out.push_back(json{{"id", late[i]["id"]}, {"title", late[i]["title"]}, {"due", late[i].value("due", "")}});
    }
    return json{{"late_count", g.late.size()},
                {"first_late", first_late},
                {"late", late_out},
                {"today", today},
                {"inbox_count", inbox_count}};
}

/// Active goals {id, title, kind, due, pace, progress, days_left}: behind first, then by due; passed and failed left
/// out.
inline json goals_block(const json& goals_with_progress) {
    std::vector<json> kept;
    for (const auto& g : goals_with_progress) {
        const std::string pace = g["progress"].value("pace", "");
        if (pace == "passed" || pace == "failed") {
            continue;
        }
        const json& p = g["progress"];
        kept.push_back(json{{"id", g["id"]},
                            {"title", g["title"]},
                            {"kind", g["kind"]},
                            {"due", g["due"]},
                            {"pace", pace},
                            {"progress", p.contains("progress") ? p["progress"] : json()},
                            {"days_left", p.contains("days_left") ? p["days_left"] : json()}});
    }
    std::stable_sort(kept.begin(), kept.end(), [](const json& a, const json& b) {
        const bool ab = a["pace"] == "behind";
        const bool bb = b["pace"] == "behind";
        if (ab != bb) {
            return ab;
        }
        return a["due"].get<std::string>() < b["due"].get<std::string>();
    });
    return json(kept);
}

/// [{currency, today, projection, median3}] per currency, by code; null when there is nothing.
/// @p today_rows are the posted rows of the day (TransactionRepository::report_rows); an
/// adjustment counts as spending as in Money::Reports. @p month_report is the period report.
inline json money_block(const json& today_rows, const json& month_report) {
    std::map<std::string, json> by_currency;
    const auto entry = [&](const std::string& currency) -> json& {
        auto it = by_currency.find(currency);
        if (it == by_currency.end()) {
            it = by_currency
                     .emplace(
                         currency,
                         json{{"currency", currency}, {"today", 0.0}, {"projection", nullptr}, {"median3", nullptr}})
                     .first;
        }
        return it->second;
    };
    for (const auto& r : today_rows) {
        if (r.value("type", "") == "income") {
            continue;
        }
        json& e = entry(r["currency"].get<std::string>());
        e["today"] = e["today"].get<double>() + r["amount"].get<double>();
    }
    if (month_report.contains("blocks")) {
        for (const auto& b : month_report["blocks"]) {
            json& e = entry(b["currency"].get<std::string>());
            e["projection"] = b.value("projection", json());
            e["median3"] = b.value("median3_expense", json());
        }
    }
    if (by_currency.empty()) {
        return json();
    }
    json out = json::array();
    for (auto& [currency, e] : by_currency) {
        out.push_back(std::move(e));
    }
    return out;
}

namespace detail {
inline json number_or_null(const json& row, const char* key) {
    return row.is_object() && row.contains(key) && row[key].is_number() ? row[key] : json();
}
inline std::chrono::sys_days day_of(const json& ts) {
    return std::chrono::sys_days{Utils::Date::parse_ymd(ts.get<std::string>().substr(0, 10))};
}
}  // namespace detail

/// The sleep sessions that ended on @p date in the account's day: end_at is UTC
/// ("YYYY-MM-DDTHH:MM:SSZ"), the account's day is UTC shifted by @p offset_seconds
/// (Repositories::region_offset_seconds). Read the rows of the day before too.
inline json sleep_on_local_day(const json& rows, std::string_view date, long long offset_seconds) {
    using namespace std::chrono;
    const sys_days want{Utils::Date::parse_ymd(date)};
    json out = json::array();
    for (const auto& r : rows) {
        if (!r.contains("end_at") || !r["end_at"].is_string()) {
            continue;
        }
        const std::string end = r["end_at"].get<std::string>();
        if (end.size() < 19) {
            continue;
        }
        const sys_seconds at = sys_days{Utils::Date::parse_ymd(end.substr(0, 10))} +
                               hours{std::stoi(end.substr(11, 2))} + minutes{std::stoi(end.substr(14, 2))} +
                               seconds{std::stoi(end.substr(17, 2))};
        if (floor<days>(at + seconds{offset_seconds}) == want) {
            out.push_back(r);
        }
    }
    return out;
}

/// {steps, active_kcal, resting_bpm, sleep {minutes, start, end, stages} | null, sleep_minutes,
/// weight_kg, weight_change_7d}; null when steps, sleep and weight are all missing.
/// @p summary_row is the summary row of the date or null; @p sleep_rows the day's sleep sessions;
/// @p body_rows the weighings of the last 8 days, oldest first.
inline json health_block(const json& summary_row, const json& sleep_rows, const json& body_rows) {
    json sleep;
    std::string latest_end;
    for (const auto& s : sleep_rows) {
        const bool nap = s.contains("is_nap") && s["is_nap"].is_boolean() && s["is_nap"].get<bool>();
        const bool usable = s.contains("time_asleep_minutes") && s["time_asleep_minutes"].is_number() &&
                            s.contains("end_at") && s["end_at"].is_string();
        if (nap || !usable) {
            continue;
        }
        const std::string end = s["end_at"].get<std::string>();
        if (sleep.is_null() || end > latest_end) {
            latest_end = end;
            sleep = json{{"minutes", s["time_asleep_minutes"]},
                         {"start", s.contains("start_at") ? s["start_at"] : json()},
                         {"end", s["end_at"]},
                         {"stages", s.contains("stages") && s["stages"].is_array() ? s["stages"] : json::array()}};
        }
    }
    json weight;
    json change;
    std::vector<json> weighed;
    for (const auto& b : body_rows) {
        if (b.contains("weight_kg") && b["weight_kg"].is_number() && b.contains("timestamp") &&
            b["timestamp"].is_string()) {
            weighed.push_back(b);
        }
    }
    if (!weighed.empty()) {
        const json& last = weighed.back();
        weight = last["weight_kg"];
        const auto last_day = detail::day_of(last["timestamp"]);
        for (auto it = weighed.rbegin(); it != weighed.rend(); ++it) {
            if ((last_day - detail::day_of((*it)["timestamp"])).count() >= kWeightChangeDays) {
                const double d = last["weight_kg"].get<double>() - (*it)["weight_kg"].get<double>();
                change = std::round(d * 10.0) / 10.0;
                break;
            }
        }
    }
    const json steps = detail::number_or_null(summary_row, "steps");
    if (steps.is_null() && sleep.is_null() && weight.is_null()) {
        return json();
    }
    return json{{"steps", steps},
                {"active_kcal", detail::number_or_null(summary_row, "active_kcal")},
                {"resting_bpm", detail::number_or_null(summary_row, "resting_bpm")},
                {"sleep", sleep},
                {"sleep_minutes", sleep.is_null() ? json() : sleep["minutes"]},
                {"weight_kg", weight},
                {"weight_change_7d", change}};
}

/// Up to five spends of the day as the ledger lists them (newest first): {id, name, amount, currency}.
/// The amount is the final one (with adjustments), positive as stored; currencies stay apart.
inline json spends_block(const json& rows) {
    json out = json::array();
    for (const auto& r : rows) {
        if (out.size() >= kSpendsShown) {
            break;
        }
        std::string name = r.contains("name") && r["name"].is_string() ? r["name"].get<std::string>() : "";
        if (name.empty() && r.contains("merchant") && r["merchant"].is_string()) {
            name = r["merchant"].get<std::string>();
        }
        if (name.empty()) {
            name = "Spend";
        }
        const json& amount =
            r.contains("final_amount") && r["final_amount"].is_number() ? r["final_amount"] : r["amount"];
        out.push_back(
            json{{"id", r["id"]}, {"name", name}, {"amount", amount.get<double>()}, {"currency", r["currency"]}});
    }
    return out;
}

}  // namespace Now::Today
