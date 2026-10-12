/**
 * @file Today.hpp
 * @brief The day's digest of the Now screen (GET /api/v1/today): each
 *        section is a part that may be off, empty or failing on its own, and
 *        the blocks are cut from what the sections already compute. No rule
 *        of its own beyond ordering; the words are the frontend's.
 */

#pragma once

#include <algorithm>
#include <exception>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "tasks/Agenda.hpp"

namespace Now::Today {

using json = nlohmann::json;

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

/// {late_count, first_late: {id, title, due} | null, today: [{id, title}], inbox_count}.
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
            today.push_back(json{{"id", row["id"]}, {"title", row["title"]}});
        }
    }
    return json{
        {"late_count", g.late.size()}, {"first_late", first_late}, {"today", today}, {"inbox_count", inbox_count}};
}

/// Active goals {id, title, kind, due, pace}: behind first, then by due; passed and failed left out.
inline json goals_block(const json& goals_with_progress) {
    std::vector<json> kept;
    for (const auto& g : goals_with_progress) {
        const std::string pace = g["progress"].value("pace", "");
        if (pace == "passed" || pace == "failed") {
            continue;
        }
        kept.push_back(
            json{{"id", g["id"]}, {"title", g["title"]}, {"kind", g["kind"]}, {"due", g["due"]}, {"pace", pace}});
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

}  // namespace Now::Today
