/**
 * @file FoodController.cpp
 * @brief Bodies for src/api/FoodController.hpp — compiled once into app_core.
 */

#include "api/FoodController.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "api/RequestUtils.hpp"
#include "api/Validation.hpp"
#include "cache/Cache.hpp"
#include "core/Modules.hpp"
#include "food/Goals.hpp"
#include "food/Http.hpp"
#include "food/Llm.hpp"
#include "food/OpenFoodFacts.hpp"
#include "jobs/FoodParseHandler.hpp"
#include "jobs/Jobs.hpp"
#include "repositories/food/EntryRepository.hpp"
#include "repositories/food/GoalsRepository.hpp"
#include "repositories/food/ItemRepository.hpp"
#include "repositories/food/ParseJobRepository.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Utf8.hpp"

namespace Api {

using json = nlohmann::json;

namespace {

constexpr int kDefaultLimit = 50;
constexpr int kMaxLimit = 200;
constexpr int kRecentLimit = 30;
constexpr std::size_t kNameMax = 200;
constexpr std::size_t kNoteMax = 500;
constexpr std::size_t kProfileNoteMax = 1000;
constexpr std::size_t kParseTextMax = 2000;
constexpr std::size_t kBatchMax = 50;
constexpr long kParseOpenMax = 3;
constexpr std::size_t kServingLabelMax = 60;
constexpr double kServingGramsMax = 5000;
constexpr std::size_t kServingsMax = 20;
constexpr double kKcalMax = 10000;
constexpr double kGramsMax = 10000;
constexpr double kMacroMax = 1000;
constexpr long kOffCacheSeconds = 3600;

const std::vector<std::string> kMeals = {"breakfast", "lunch", "dinner", "snack"};
const std::vector<std::string> kPer = {"100g", "100ml"};
const std::vector<std::string> kSexes = {"male", "female"};
const std::vector<std::string> kActivities = {"sedentary", "light", "moderate", "active", "very_active"};
const std::regex kDateRe(R"(^\d{4}-\d{2}-\d{2}$)");

/// Like Validation::string_length, but in characters: the table CHECKs count
/// characters too, and a Cyrillic name must not get half the room.
void text_length(Validation::Errors& errs, const json& body, const std::string& field, std::size_t lo, std::size_t hi) {
    if (!body.contains(field) || body[field].is_null()) {
        return;
    }
    if (!body[field].is_string()) {
        errs.add(field, "invalid", "must be a string");
        return;
    }
    const std::size_t n = Utils::Utf8::length(body[field].get<std::string>());
    if (n < lo) {
        errs.add(field, "too_short", "min length " + std::to_string(lo));
    } else if (n > hi) {
        errs.add(field, "too_long", "max length " + std::to_string(hi));
    }
}

/// Rows belong to an app user; a static-bearer principal has no user id.
bool require_user(const std::string& owner, const FoodController::Callback& callback) {
    if (is_valid_uuid(owner)) {
        return true;
    }
    callback(ErrorResponse::forbidden("no_user_account", "this route needs a user account"));
    return false;
}

std::string today_utc() {
    const std::chrono::year_month_day ymd{std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())};
    char out[16];
    std::snprintf(out,
                  sizeof(out),
                  "%04d-%02u-%02u",
                  static_cast<int>(ymd.year()),
                  static_cast<unsigned>(ymd.month()),
                  static_cast<unsigned>(ymd.day()));
    return out;
}

/// Optional number within [lo, hi]; null is "unset".
void number_range(Validation::Errors& errs, const json& body, const std::string& field, double lo, double hi) {
    if (!body.contains(field) || body[field].is_null()) {
        return;
    }
    if (!body[field].is_number()) {
        errs.add(field, "not_number", "must be a number");
        return;
    }
    const double v = body[field].get<double>();
    if (!(v >= lo && v <= hi)) {
        errs.add(field, "out_of_range", "must be in " + json(lo).dump() + ".." + json(hi).dump());
    }
}

std::optional<double> opt_number(const json& body, const std::string& field) {
    if (body.contains(field) && body[field].is_number()) {
        return body[field].get<double>();
    }
    return std::nullopt;
}

std::optional<int> opt_int(const json& body, const std::string& field) {
    if (body.contains(field) && body[field].is_number_integer()) {
        return body[field].get<int>();
    }
    return std::nullopt;
}

/// A real calendar day between 1900 and 2100: the regex alone lets "2026-02-30" through.
bool is_calendar_date(const std::string& text) {
    if (!std::regex_match(text, kDateRe)) {
        return false;
    }
    try {
        const auto ymd = Food::Goals::detail::parse_ymd(text);
        const int year = static_cast<int>(ymd.year());
        return year >= 1900 && year <= 2100;
    } catch (const std::invalid_argument&) {
        return false;
    }
}

void date_field(Validation::Errors& errs, const json& body, const std::string& field) {
    if (!body.contains(field) || body[field].is_null()) {
        return;
    }
    if (!body[field].is_string() || !is_calendar_date(body[field].get<std::string>())) {
        errs.add(field, "bad_format", "expected a calendar day as YYYY-MM-DD");
    }
}

/// `[{label, grams}]`, at most kServingsMax entries; returns the normalized JSON text.
std::optional<std::string> servings_json(Validation::Errors& errs, const json& body) {
    if (!body.contains("servings") || body["servings"].is_null()) {
        return std::nullopt;
    }
    const auto& list = body["servings"];
    if (!list.is_array() || list.size() > kServingsMax) {
        errs.add("servings", "invalid", "must be an array of at most 20 {label, grams}");
        return std::nullopt;
    }
    json clean = json::array();
    for (const auto& s : list) {
        const bool ok =
            s.is_object() && s.contains("label") && s["label"].is_string() && !s["label"].get<std::string>().empty() &&
            s["label"].get<std::string>().size() <= kServingLabelMax && s.contains("grams") && s["grams"].is_number() &&
            s["grams"].get<double>() > 0 && s["grams"].get<double>() <= kServingGramsMax;
        if (!ok) {
            errs.add("servings", "invalid", "each serving needs a label (1..60) and grams (0..5000)");
            return std::nullopt;
        }
        clean.push_back(json{{"label", s["label"]}, {"grams", s["grams"]}});
    }
    return clean.dump();
}

/// The nutrient fields of an item or a quick entry.
void nutrient_fields(Validation::Errors& errs, const json& body) {
    number_range(errs, body, "kcal", 0, kKcalMax);
    for (const char* f : {"protein_g", "fat_g", "carbs_g", "fiber_g", "sugar_g", "salt_g"}) {
        number_range(errs, body, f, 0, kMacroMax);
    }
}

json product_json(const Food::Off::Product& p) {
    json out{{"code", p.code},
             {"name", p.name},
             {"brand", p.brand},
             {"per", p.per},
             {"kcal", p.kcal},
             {"protein_g", p.protein_g},
             {"fat_g", p.fat_g},
             {"carbs_g", p.carbs_g},
             {"fiber_g", p.fiber_g ? json(*p.fiber_g) : json()},
             {"sugar_g", p.sugar_g ? json(*p.sugar_g) : json()},
             {"salt_g", p.salt_g ? json(*p.salt_g) : json()},
             {"serving_grams", p.serving_grams ? json(*p.serving_grams) : json()},
             {"serving_label", p.serving_label}};
    return out;
}

/// The goals block of GET/PUT goals and the targets of a day: the profile,
/// the weight with its source, the computed numbers and the targets after
/// the overrides. Targets are null until the profile is complete.
json goals_body(const std::string& owner) {
    Repositories::GoalsRepository goals;
    json profile = goals.load(owner).value_or(json{{"height_cm", nullptr},
                                                   {"birth_date", nullptr},
                                                   {"sex", nullptr},
                                                   {"activity", "light"},
                                                   {"target_weight_kg", nullptr},
                                                   {"pace_kg_per_week", 0},
                                                   {"manual_weight_kg", nullptr},
                                                   {"profile_note", ""},
                                                   {"kcal_override", nullptr},
                                                   {"protein_override_g", nullptr},
                                                   {"fat_override_g", nullptr},
                                                   {"carbs_override_g", nullptr},
                                                   {"updated_at", nullptr}});
    json weight{{"kg", nullptr}, {"source", "none"}};
    if (const auto scale = goals.scale_weight(owner)) {
        weight = {{"kg", *scale}, {"source", "scale"}};
    } else if (profile["manual_weight_kg"].is_number()) {
        weight = {{"kg", profile["manual_weight_kg"]}, {"source", "manual"}};
    }

    json missing = json::array();
    for (const char* f : {"height_cm", "birth_date", "sex", "target_weight_kg"}) {
        if (profile[f].is_null()) {
            missing.push_back(f);
        }
    }
    if (weight["kg"].is_null()) {
        missing.push_back("weight");
    }

    json computed;
    json targets;
    if (missing.empty()) {
        Food::Goals::Inputs in;
        in.weight_kg = weight["kg"].get<double>();
        in.height_cm = profile["height_cm"].get<int>();
        in.age_years = Food::Goals::age_on(profile["birth_date"].get<std::string>(), today_utc());
        in.sex = profile["sex"].get<std::string>();
        in.activity = profile["activity"].get<std::string>();
        in.target_weight_kg = profile["target_weight_kg"].get<double>();
        in.pace_kg_per_week = profile["pace_kg_per_week"].get<double>();
        const auto c = Food::Goals::compute(in);
        computed = {{"age_years", in.age_years},
                    {"bmr", c.bmr},
                    {"maintenance", c.maintenance},
                    {"deficit", c.deficit},
                    {"kcal", c.kcal},
                    {"floored", c.floored},
                    {"below_bmr", c.below_bmr},
                    {"protein_g", c.protein_g},
                    {"fat_g", c.fat_g},
                    {"carbs_g", c.carbs_g}};
        const auto t = Food::Goals::apply_overrides(c,
                                                    opt_int(profile, "kcal_override"),
                                                    opt_int(profile, "protein_override_g"),
                                                    opt_int(profile, "fat_override_g"),
                                                    opt_int(profile, "carbs_override_g"));
        targets = {{"kcal", t.kcal}, {"protein_g", t.protein_g}, {"fat_g", t.fat_g}, {"carbs_g", t.carbs_g}};
    } else if (profile["kcal_override"].is_number() && profile["protein_override_g"].is_number() &&
               profile["fat_override_g"].is_number() && profile["carbs_override_g"].is_number()) {
        // Everything set by hand: no profile needed.
        targets = {{"kcal", profile["kcal_override"]},
                   {"protein_g", profile["protein_override_g"]},
                   {"fat_g", profile["fat_override_g"]},
                   {"carbs_g", profile["carbs_override_g"]}};
    }
    return json{{"profile", profile},
                {"weight", weight},
                {"missing", missing},
                {"computed", computed},
                {"targets", targets},
                // Whether the add form may offer the text parse.
                {"llm_available", Food::Llm::settings().has_value()}};
}

/// Validates one entry body; fills @p out. Returns false after adding errors.
bool entry_input(Validation::Errors& errs,
                 const json& body,
                 const std::string& prefix,
                 const std::string& owner,
                 Repositories::EntryRepository::Input& out) {
    const auto at = [&](const char* f) { return prefix + f; };
    if (!body.is_object()) {
        errs.add(prefix.empty() ? "body" : prefix, "invalid", "must be an object");
        return false;
    }
    Validation::Errors local;
    Validation::require_string(local, body, "date");
    date_field(local, body, "date");
    Validation::require_string(local, body, "meal");
    Validation::one_of(local, body, "meal", kMeals);
    if (body.contains("item_id") && !body["item_id"].is_null()) {
        Validation::uuid(local, body, "item_id");
    }
    number_range(local, body, "grams", 0.01, kGramsMax);
    if (body.contains("name")) {
        text_length(local, body, "name", 1, kNameMax);
    }
    if (body.contains("note")) {
        text_length(local, body, "note", 0, kNoteMax);
    }
    nutrient_fields(local, body);
    const bool has_item = body.contains("item_id") && body["item_id"].is_string();
    // require() rejects a missing or null value; the type is checked by the
    // range and length validators above.
    if (has_item) {
        Validation::require(local, body, "grams");
    } else {
        Validation::require_string(local, body, "name");
        Validation::require(local, body, "kcal");
        if (body.contains("kcal") && !body["kcal"].is_number()) {
            local.add("kcal", "invalid", "must be a number");
        }
    }
    if (local.any()) {
        for (const auto& e : local.errors_json()) {
            errs.add(at(e.value("field", "").c_str()), e.value("code", ""), e.value("message", ""));
        }
        return false;
    }

    out.date = body["date"].get<std::string>();
    out.meal = body["meal"].get<std::string>();
    out.note = Validation::opt_string(body, "note").value_or("");
    if (has_item) {
        out.item_id = body["item_id"].get<std::string>();
        out.grams = body["grams"].get<double>();
        const auto n = Repositories::EntryRepository().from_item(owner, *out.item_id, *out.grams);
        if (!n.has_value()) {
            throw Repositories::FoodItemNotFound();
        }
        out.name = Validation::opt_string(body, "name").value_or(n->name);
        out.kcal = n->kcal;
        out.protein_g = n->protein_g;
        out.fat_g = n->fat_g;
        out.carbs_g = n->carbs_g;
        out.fiber_g = n->fiber_g;
        out.sugar_g = n->sugar_g;
        out.salt_g = n->salt_g;
        out.estimated = false;
    } else {
        out.name = body["name"].get<std::string>();
        out.grams = opt_number(body, "grams");
        out.kcal = body["kcal"].get<double>();
        out.protein_g = opt_number(body, "protein_g").value_or(0);
        out.fat_g = opt_number(body, "fat_g").value_or(0);
        out.carbs_g = opt_number(body, "carbs_g").value_or(0);
        out.fiber_g = opt_number(body, "fiber_g");
        out.sugar_g = opt_number(body, "sugar_g");
        out.salt_g = opt_number(body, "salt_g");
        out.estimated = true;
    }
    return true;
}

}  // namespace

// Guards. Order: module -> identity. API_REQUIRE_OWNER declares `owner`.
#define FOOD_GUARD(req, callback, owner)     \
    if (!require_enabled(callback))          \
        return;                              \
    API_REQUIRE_OWNER(req, callback, owner); \
    if (!require_user(owner, callback))      \
    return

bool FoodController::require_enabled(const Callback& callback) {
    if (Core::food_enabled()) {
        return true;
    }
    callback(ErrorResponse::not_found("food"));
    return false;
}

// ── goals ────────────────────────────────────────────────────────────────────

void FoodController::getGoals(const HttpRequestPtr& req, Callback&& callback) {
    FOOD_GUARD(req, callback, owner);
    with_repo_errors(callback, "food.getGoals", [&] { callback(Response::ok(json{{"data", goals_body(owner)}})); });
}

void FoodController::putGoals(const HttpRequestPtr& req, Callback&& callback) {
    FOOD_GUARD(req, callback, owner);
    json body;
    if (!Validation::parse_body(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Validation::int_range(errs, body, "height_cm", 100, 250);
    date_field(errs, body, "birth_date");
    if (body.contains("sex") && !body["sex"].is_null()) {
        Validation::one_of(errs, body, "sex", kSexes);
    }
    if (body.contains("activity")) {
        Validation::one_of(errs, body, "activity", kActivities);
    }
    number_range(errs, body, "target_weight_kg", 30, 300);
    number_range(errs, body, "pace_kg_per_week", -1, 1.5);
    number_range(errs, body, "manual_weight_kg", 30, 300);
    if (body.contains("profile_note")) {
        text_length(errs, body, "profile_note", 0, kProfileNoteMax);
    }
    Validation::int_range(errs, body, "kcal_override", 500, 10000);
    Validation::int_range(errs, body, "protein_override_g", 0, 1000);
    Validation::int_range(errs, body, "fat_override_g", 0, 1000);
    Validation::int_range(errs, body, "carbs_override_g", 0, 2000);
    if (!errs.any() && body.contains("birth_date") && body["birth_date"].is_string()) {
        try {
            Food::Goals::age_on(body["birth_date"].get<std::string>(), today_utc());
        } catch (const std::invalid_argument&) {
            errs.add("birth_date", "invalid", "must be a calendar day in the past");
        }
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }

    Repositories::GoalsRepository::Profile p;
    p.height_cm = opt_int(body, "height_cm");
    p.birth_date = Validation::opt_string(body, "birth_date");
    p.sex = Validation::opt_string(body, "sex");
    p.activity = Validation::opt_string(body, "activity").value_or("light");
    p.target_weight_kg = opt_number(body, "target_weight_kg");
    p.pace_kg_per_week = opt_number(body, "pace_kg_per_week").value_or(0);
    p.manual_weight_kg = opt_number(body, "manual_weight_kg");
    p.profile_note = Validation::opt_string(body, "profile_note").value_or("");
    p.kcal_override = opt_int(body, "kcal_override");
    p.protein_override_g = opt_int(body, "protein_override_g");
    p.fat_override_g = opt_int(body, "fat_override_g");
    p.carbs_override_g = opt_int(body, "carbs_override_g");

    with_repo_errors(callback, "food.putGoals", [&] {
        Repositories::GoalsRepository().put(owner, p);
        callback(Response::ok(json{{"data", goals_body(owner)}}));
    });
}

// ── items ────────────────────────────────────────────────────────────────────

void FoodController::listItems(const HttpRequestPtr& req, Callback&& callback) {
    FOOD_GUARD(req, callback, owner);
    const std::string q = req->getParameter("q");
    if (q.size() > kNameMax) {
        callback(ErrorResponse::bad_request("invalid_query", "q is too long"));
        return;
    }
    const auto page = parse_page_params(req, kDefaultLimit, kMaxLimit);
    with_repo_errors(callback, "food.listItems", [&] {
        const auto result = Repositories::ItemRepository().list(
            owner, q, req->getParameter("archived") == "true", page.limit, page.offset);
        callback(Response::ok(json{{"data", result.rows}, {"count", result.rows.size()}, {"total", result.total}}));
    });
}

void FoodController::createItem(const HttpRequestPtr& req, Callback&& callback) {
    FOOD_GUARD(req, callback, owner);
    json body;
    if (!Validation::parse_body(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Validation::require_string(errs, body, "name");
    text_length(errs, body, "name", 1, kNameMax);
    if (body.contains("brand")) {
        text_length(errs, body, "brand", 0, kNameMax);
    }
    if (body.contains("per")) {
        Validation::one_of(errs, body, "per", kPer);
    }
    Validation::require(errs, body, "kcal");
    nutrient_fields(errs, body);
    const auto servings = servings_json(errs, body);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    Repositories::ItemRepository::Input in;
    in.name = body["name"].get<std::string>();
    in.brand = Validation::opt_string(body, "brand").value_or("");
    in.per = Validation::opt_string(body, "per").value_or("100g");
    in.kcal = body["kcal"].get<double>();
    in.protein_g = opt_number(body, "protein_g").value_or(0);
    in.fat_g = opt_number(body, "fat_g").value_or(0);
    in.carbs_g = opt_number(body, "carbs_g").value_or(0);
    in.fiber_g = opt_number(body, "fiber_g");
    in.sugar_g = opt_number(body, "sugar_g");
    in.salt_g = opt_number(body, "salt_g");
    in.servings_json = servings.value_or("[]");
    with_repo_errors(callback, "food.createItem", [&] {
        callback(Response::created(json{{"data", Repositories::ItemRepository().create(owner, in)}}));
    });
}

void FoodController::updateItem(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    FOOD_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!Validation::parse_body(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    if (body.contains("name")) {
        text_length(errs, body, "name", 1, kNameMax);
    }
    if (body.contains("brand")) {
        text_length(errs, body, "brand", 0, kNameMax);
    }
    if (body.contains("per")) {
        Validation::one_of(errs, body, "per", kPer);
    }
    nutrient_fields(errs, body);
    if (body.contains("archived")) {
        Validation::boolean(errs, body, "archived");
    }
    const auto servings = servings_json(errs, body);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    Repositories::ItemRepository::Patch p;
    p.name = Validation::opt_string(body, "name");
    p.brand = Validation::opt_string(body, "brand");
    p.per = Validation::opt_string(body, "per");
    p.kcal = opt_number(body, "kcal");
    p.protein_g = opt_number(body, "protein_g");
    p.fat_g = opt_number(body, "fat_g");
    p.carbs_g = opt_number(body, "carbs_g");
    // Nullable nutrients: present means set (a number or null), absent means leave.
    if (body.contains("fiber_g")) {
        p.fiber_g = opt_number(body, "fiber_g");
    }
    if (body.contains("sugar_g")) {
        p.sugar_g = opt_number(body, "sugar_g");
    }
    if (body.contains("salt_g")) {
        p.salt_g = opt_number(body, "salt_g");
    }
    p.servings_json = servings;
    if (body.contains("archived") && body["archived"].is_boolean()) {
        p.archived = body["archived"].get<bool>();
    }
    with_repo_errors(callback, "food.updateItem", [&] {
        callback(Response::ok(json{{"data", Repositories::ItemRepository().update(owner, id, p)}}));
    });
}

void FoodController::deleteItem(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    FOOD_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "food.deleteItem", [&] {
        const std::string outcome = Repositories::ItemRepository().remove(owner, id);
        callback(Response::ok(
            json{{"message", outcome == "archived" ? "Item archived" : "Item deleted"}, {"outcome", outcome}}));
    });
}

void FoodController::recentItems(const HttpRequestPtr& req, Callback&& callback) {
    FOOD_GUARD(req, callback, owner);
    with_repo_errors(callback, "food.recentItems", [&] {
        const auto rows = Repositories::ItemRepository().recent(owner, kRecentLimit);
        callback(Response::ok(json{{"data", rows}, {"count", rows.size()}, {"total", rows.size()}}));
    });
}

// ── Open Food Facts ──────────────────────────────────────────────────────────

void FoodController::offSearch(const HttpRequestPtr& req, Callback&& callback) {
    FOOD_GUARD(req, callback, owner);
    std::string q = req->getParameter("q");
    if (q.empty() || q.size() > Food::Off::kMaxQueryLength) {
        callback(ErrorResponse::bad_request("invalid_query", "q must be 1..80 characters"));
        return;
    }
    // Same query, same answer for an hour: the service asks to go easy on it.
    std::string key = "food:off:";
    for (const char c : q) {
        key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (Cache::is_initialized()) {
        if (const auto cached = Cache::get().get(key)) {
            const json data = json::parse(*cached, nullptr, /*allow_exceptions=*/false);
            if (data.is_array()) {
                callback(Response::ok(json{{"data", data}, {"count", data.size()}, {"cached", true}}));
                return;
            }
        }
    }
    try {
        json data = json::array();
        for (const auto& p : Food::Off::search(Food::Http::transport(), q)) {
            data.push_back(product_json(p));
        }
        if (Cache::is_initialized()) {
            Cache::get().set(key, data.dump(), kOffCacheSeconds);
        }
        callback(Response::ok(json{{"data", data}, {"count", data.size()}, {"cached", false}}));
    } catch (const Food::Off::Unavailable& e) {
        spdlog::warn("open food facts search unavailable: {}", e.what());
        callback(ErrorResponse::service_unavailable("upstream_unavailable", "Open Food Facts is not available now"));
    }
}

void FoodController::itemFromOff(const HttpRequestPtr& req, Callback&& callback) {
    FOOD_GUARD(req, callback, owner);
    json body;
    if (!Validation::parse_body(req, body, callback)) {
        return;
    }
    const auto code = Validation::opt_string(body, "code").value_or("");
    if (code.empty() || code.size() > 20 ||
        !std::all_of(code.begin(), code.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        callback(ErrorResponse::bad_request("invalid_code", "code must be 1..20 digits"));
        return;
    }
    std::optional<Food::Off::Product> product;
    try {
        product = Food::Off::product(Food::Http::transport(), code);
    } catch (const Food::Off::Unavailable& e) {
        spdlog::warn("open food facts product unavailable: {}", e.what());
        callback(ErrorResponse::service_unavailable("upstream_unavailable", "Open Food Facts is not available now"));
        return;
    }
    if (!product.has_value()) {
        callback(ErrorResponse::not_found("off_product"));
        return;
    }
    Repositories::ItemRepository::Input in;
    in.source = "off";
    in.off_code = product->code.empty() ? code : product->code;
    // The copy obeys the limits of POST /items: Open Food Facts data is user-contributed.
    in.name = Utils::Utf8::cut(product->name, kNameMax);
    in.brand = Utils::Utf8::cut(product->brand, kNameMax);
    in.per = product->per;
    in.kcal = std::min(product->kcal, kKcalMax);
    in.protein_g = product->protein_g;
    in.fat_g = product->fat_g;
    in.carbs_g = product->carbs_g;
    in.fiber_g = product->fiber_g;
    in.sugar_g = product->sugar_g;
    in.salt_g = product->salt_g;
    if (product->serving_grams.has_value() && *product->serving_grams > 0 &&
        *product->serving_grams <= kServingGramsMax) {
        const std::string label = Utils::Utf8::cut(
            product->serving_label.empty() ? std::string("1 serving") : product->serving_label, kServingLabelMax);
        in.servings_json = json::array({json{{"label", label}, {"grams", *product->serving_grams}}}).dump();
    }
    with_repo_errors(callback, "food.itemFromOff", [&] {
        const auto [item, created] = Repositories::ItemRepository().upsert_off(owner, in);
        callback(created ? Response::created(json{{"data", item}}) : Response::ok(json{{"data", item}}));
    });
}

// ── diary ────────────────────────────────────────────────────────────────────

void FoodController::day(const HttpRequestPtr& req, Callback&& callback) {
    FOOD_GUARD(req, callback, owner);
    const std::string date = req->getParameter("date").empty() ? today_utc() : req->getParameter("date");
    if (!is_calendar_date(date)) {
        callback(ErrorResponse::bad_request("invalid_date", "date must be a calendar day as YYYY-MM-DD"));
        return;
    }
    with_repo_errors(callback, "food.day", [&] {
        json out = Repositories::EntryRepository().day(owner, date);
        const auto active = Repositories::GoalsRepository().active_kcal(owner, date);
        out["active_kcal"] = active ? json(*active) : json();
        out["targets"] = goals_body(owner)["targets"];
        callback(Response::ok(json{{"data", out}}));
    });
}

void FoodController::week(const HttpRequestPtr& req, Callback&& callback) {
    FOOD_GUARD(req, callback, owner);
    const std::string from = req->getParameter("from");
    if (!is_calendar_date(from)) {
        callback(ErrorResponse::bad_request("invalid_date", "from must be a calendar day as YYYY-MM-DD"));
        return;
    }
    with_repo_errors(callback, "food.week", [&] {
        callback(Response::ok(json{{"data",
                                    {{"from", from},
                                     {"days", Repositories::EntryRepository().week(owner, from)},
                                     {"targets", goals_body(owner)["targets"]}}}}));
    });
}

void FoodController::createEntry(const HttpRequestPtr& req, Callback&& callback) {
    FOOD_GUARD(req, callback, owner);
    json body;
    if (!Validation::parse_body(req, body, callback)) {
        return;
    }
    with_repo_errors(callback, "food.createEntry", [&] {
        Validation::Errors errs;
        Repositories::EntryRepository::Input in;
        if (!entry_input(errs, body, "", owner, in)) {
            callback(Validation::response_400(errs));
            return;
        }
        callback(Response::created(json{{"data", Repositories::EntryRepository().create(owner, in)}}));
    });
}

void FoodController::createEntries(const HttpRequestPtr& req, Callback&& callback) {
    FOOD_GUARD(req, callback, owner);
    json body;
    if (!Validation::parse_body(req, body, callback)) {
        return;
    }
    if (!body.is_object() || !body.contains("entries") || !body["entries"].is_array() || body["entries"].empty() ||
        body["entries"].size() > kBatchMax) {
        callback(ErrorResponse::bad_request("invalid_body", "entries must be an array of 1..50 entries"));
        return;
    }
    with_repo_errors(callback, "food.createEntries", [&] {
        Validation::Errors errs;
        std::vector<Repositories::EntryRepository::Input> inputs;
        std::size_t index = 0;
        for (const auto& item : body["entries"]) {
            Repositories::EntryRepository::Input in;
            if (entry_input(errs, item, "entries[" + std::to_string(index) + "].", owner, in)) {
                inputs.push_back(std::move(in));
            }
            ++index;
        }
        if (errs.any()) {
            callback(Validation::response_400(errs));
            return;
        }
        const auto rows = Repositories::EntryRepository().create_many(owner, inputs);
        callback(Response::created(json{{"data", rows}, {"count", rows.size()}}));
    });
}

void FoodController::updateEntry(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    FOOD_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!Validation::parse_body(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    date_field(errs, body, "date");
    if (body.contains("meal")) {
        Validation::one_of(errs, body, "meal", kMeals);
    }
    number_range(errs, body, "grams", 0.01, kGramsMax);
    if (body.contains("note")) {
        text_length(errs, body, "note", 0, kNoteMax);
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    Repositories::EntryRepository::Patch p;
    p.date = Validation::opt_string(body, "date");
    p.meal = Validation::opt_string(body, "meal");
    p.grams = opt_number(body, "grams");
    p.note = Validation::opt_string(body, "note");
    with_repo_errors(callback, "food.updateEntry", [&] {
        callback(Response::ok(json{{"data", Repositories::EntryRepository().update(owner, id, p)}}));
    });
}

void FoodController::deleteEntry(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    FOOD_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "food.deleteEntry", [&] {
        Repositories::EntryRepository().remove(owner, id);
        callback(Response::ok(json{{"message", "Entry deleted"}}));
    });
}

// ── parse ────────────────────────────────────────────────────────────────────

void FoodController::parseStart(const HttpRequestPtr& req, Callback&& callback) {
    FOOD_GUARD(req, callback, owner);
    json body;
    if (!Validation::parse_body(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Validation::require_string(errs, body, "text");
    text_length(errs, body, "text", 1, kParseTextMax);
    Validation::require_string(errs, body, "meal");
    Validation::one_of(errs, body, "meal", kMeals);
    Validation::require_string(errs, body, "date");
    date_field(errs, body, "date");
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    // The API checks only that the worker's settings exist; the worker talks to the provider.
    if (!Food::Llm::settings().has_value()) {
        callback(ErrorResponse::service_unavailable("not_configured", "the text parse is not set up on this server"));
        return;
    }
    Repositories::ParseJobRepository repo;
    std::string id;
    try {
        // Each parse costs provider tokens: a few in flight per user is plenty.
        if (repo.open_count(owner) >= kParseOpenMax) {
            callback(ErrorResponse::make({drogon::k429TooManyRequests,
                                          "too_many_parses",
                                          "wait for your running parses to finish",
                                          json{{"retry_after_sec", 30}}}));
            return;
        }
        const json job = repo.create(
            owner, body["text"].get<std::string>(), body["meal"].get<std::string>(), body["date"].get<std::string>());
        id = job["id"].get<std::string>();
    } catch (const std::exception& e) {
        spdlog::warn("food parse create failed: {}", e.what());
        callback(ErrorResponse::service_unavailable("storage_unavailable"));
        return;
    }
    try {
        // The worker may run with another retry limit; the job carries the one it was submitted with.
        Jobs::get().submit(
            Jobs::FoodParse::kJobType,
            json{{"job_id", id}, {"owner_id", owner}, {"max_attempts", Jobs::get().default_max_retries()}});
    } catch (const std::exception& e) {
        spdlog::warn("food parse enqueue unavailable: {}", e.what());
        // Nothing will ever run it: the row must not look queued to the page.
        try {
            repo.fail(id, "queue_unavailable", "the job queue did not take the job");
        } catch (const std::exception& inner) {
            spdlog::warn("food parse {}: could not mark failed: {}", id, inner.what());
        }
        callback(ErrorResponse::service_unavailable("queue_unavailable"));
        return;
    }
    auto resp = Response::ok(json{{"data", {{"id", id}, {"status", "queued"}}}});
    resp->setStatusCode(k202Accepted);
    callback(resp);
}

void FoodController::parseStatus(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    FOOD_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "food.parseStatus", [&] {
        const auto job = Repositories::ParseJobRepository().get(owner, id);
        if (!job.has_value()) {
            callback(ErrorResponse::not_found("parse_job"));
            return;
        }
        callback(Response::ok(json{{"data", *job}}));
    });
}

#undef FOOD_GUARD

}  // namespace Api
