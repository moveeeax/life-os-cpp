/**
 * @file LedgerActions.cpp
 * @brief The write tools on the food diary and the money ledger (agent/ActionKit.hpp).
 */

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <optional>
#include <regex>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "agent/ActionKit.hpp"
#include "agent/SiteApi.hpp"
#include "money/Currencies.hpp"
#include "money/Merchant.hpp"
#include "utils/Crypto.hpp"

namespace Agent::Actions::detail {

namespace {

/// A number argument within [lo, hi]; nullopt when it is missing, not a number or out of range.
std::optional<double> number_arg(const json& args, const char* key, double lo, double hi) {
    const auto it = args.find(key);
    if (it == args.end() || !it->is_number()) {
        return std::nullopt;
    }
    const double v = it->get<double>();
    return v >= lo && v <= hi ? std::optional<double>(v) : std::nullopt;
}

/// "350", "12.5", "0.25": a number as a person writes it.
std::string number_words(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    std::string s = buf;
    while (s.back() == '0') {
        s.pop_back();
    }
    if (s.back() == '.') {
        s.pop_back();
    }
    return s;
}

/// Whether @p v is a UUID as the API writes it.
bool is_uuid(const std::string& v) {
    static const std::regex kUuid("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
    return std::regex_match(v, kUuid);
}

/// The lines of a food or spend proposal: 1 to kMaxLines objects.
const json* lines_arg(const json& args) {
    const auto it = args.find("lines");
    if (it == args.end() || !it->is_array() || it->empty() || it->size() > kMaxLines) {
        return nullptr;
    }
    for (const auto& l : *it) {
        if (!l.is_object()) {
            return nullptr;
        }
    }
    return &*it;
}

const json kMeals = {"breakfast", "lunch", "dinner", "snack"};

/// The first row of @p rows whose "id" is @p id; nullptr when none is.
const json* by_id(const json& rows, const std::string& id) {
    for (const auto& r : rows) {
        if (r.is_object() && r.value("id", "") == id) {
            return &r;
        }
    }
    return nullptr;
}

/// [{id, name, currency}] of the open accounts, of @p currency when one is given.
json account_choices(const json& accounts, const std::string& currency = "") {
    json out = json::array();
    for (const auto& a : accounts) {
        if (a.is_object() && !a.value("archived", false) && (currency.empty() || a.value("currency", "") == currency)) {
            out.push_back(json{{"id", a.value("id", "")}, {"name", a.value("name", "")}, {"currency", a["currency"]}});
        }
    }
    return out;
}

/// [{id, name}] of the open expense categories.
json category_choices(const json& categories) {
    json out = json::array();
    for (const auto& c : categories) {
        if (c.is_object() && c.value("kind", "") == "expense" && !c.value("archived", false)) {
            out.push_back(json{{"id", c.value("id", "")}, {"name", c.value("name", "")}});
        }
    }
    return out;
}

/// What the ledger remembers of @p merchant: its category and the account used last; {} when nothing.
json merchant_memory(const Ctx& c, const std::string& merchant) {
    json memory = json::object();
    if (!merchant.empty()) {
        const auto found = SiteApi::read_data(c.transport,
                                              c.api_url,
                                              c.token,
                                              "/api/v1/money/merchants",
                                              json::array({json::array({"q", merchant}), json::array({"limit", "5"})}));
        for (const auto& m : found && found->is_array() ? *found : json::array()) {
            if (m.is_object() && m.value("merchant_key", "") == ::Money::merchant_key(merchant)) {
                memory = m;
            }
        }
    }
    return memory;
}

/// The account the line @p l (at "lines[i]: ") in @p currency is paid from: its account_id, else
/// @p last_account (the merchant's) when it is open and in the currency, else the only open
/// account in the currency; the refusal the model reads when there is none.
std::variant<const json*, std::string> line_account(const json& l,
                                                    const std::string& at,
                                                    const json& accounts,
                                                    const std::string& currency,
                                                    const std::string& last_account) {
    if (const std::string id = text_arg(l, "account_id"); !id.empty()) {
        const json* account = by_id(accounts, id);
        if (account == nullptr) {
            return refusal("not_found",
                           at + "account_id is not one of the person's accounts",
                           json{{"accounts", account_choices(accounts)}});
        }
        if (account->value("currency", "") != currency) {
            return refusal("currency",
                           at + "the account is in " + account->value("currency", "") +
                               "; an amount is written in its account's currency and never converted",
                           json{{"accounts", account_choices(accounts, currency)}});
        }
        return account;
    }
    const json* last = by_id(accounts, last_account);
    if (last != nullptr && !last->value("archived", false) && last->value("currency", "") == currency) {
        return last;
    }
    const json open = account_choices(accounts, currency);
    if (open.size() != 1) {
        return refusal(open.empty() ? "no_account" : "ambiguous",
                       open.empty()
                           ? at + "no open account is in " + currency + ": ask the person which account paid it"
                           : at + "several accounts are in " + currency + ": ask which one",
                       json{{"accounts", open.empty() ? account_choices(accounts) : open}});
    }
    return by_id(accounts, open[0].value("id", ""));
}

/// "a, b, c" of @p parts.
std::string joined(const std::vector<std::string>& parts) {
    std::string out;
    for (const auto& s : parts) {
        out += (out.empty() ? "" : ", ") + s;
    }
    return out;
}

}  // namespace

json add_food_tool() {
    return tool("add_food",
                "Propose what the person ate as entries of their food diary, one line per dish with your estimate of "
                "its numbers. Nothing is written until they confirm it.",
                object({{"date", {{"type", "string"}, {"description", "A day as YYYY-MM-DD; today when left out"}}},
                        {"meal", {{"type", "string"}, {"enum", kMeals}}},
                        {"lines",
                         {{"type", "array"},
                          {"minItems", 1},
                          {"maxItems", kMaxLines},
                          {"items",
                           object({{"name", {{"type", "string"}, {"description", "The dish, in the person's words"}}},
                                   {"grams", {{"type", "number"}, {"description", "The portion in grams, if known"}}},
                                   {"item_id",
                                    {{"type", "string"},
                                     {"description", "One of the person's products; the diary takes its numbers"}}},
                                   {"kcal", {{"type", "number"}}},
                                   {"protein_g", {{"type", "number"}}},
                                   {"fat_g", {{"type", "number"}}},
                                   {"carbs_g", {{"type", "number"}}}},
                                  json::array({"name", "kcal", "protein_g", "fat_g", "carbs_g"}))}}}},
                       json::array({"meal", "lines"})));
}

json add_spend_tool() {
    return tool(
        "add_spend",
        "Propose what the person spent as posted expenses, one line per purchase, each amount in its "
        "account's currency (never converted). The account and category are found from the merchant and the "
        "currency when left out; when they cannot be, the answer lists the ids to pick from. Nothing is "
        "written until they confirm it.",
        object({{"date", {{"type", "string"}, {"description", "A day as YYYY-MM-DD; today when left out"}}},
                {"lines",
                 {{"type", "array"},
                  {"minItems", 1},
                  {"maxItems", kMaxLines},
                  {"items",
                   object({{"amount", {{"type", "number"}, {"description", "Above 0, in the currency"}}},
                           {"currency", {{"type", "string"}, {"description", "A code like KZT"}}},
                           {"account_id", {{"type", "string"}, {"description", "The account it was paid from"}}},
                           {"category_id", {{"type", "string"}, {"description", "An expense category"}}},
                           {"name", {{"type", "string"}, {"description", "What was bought"}}},
                           {"merchant", {{"type", "string"}, {"description", "Where"}}}},
                          json::array({"amount", "currency", "name"}))}}}},
               json::array({"lines"})));
}

/// Food diary entries of one meal, in one batch (POST /food/entries/batch, all or none).
Proposed add_food(const Ctx& c, const json& args) {
    const std::string date = day_arg(args, "date").empty() ? c.today : day_arg(args, "date");
    if (date.empty()) {
        return refusal("invalid", "the date is a day as YYYY-MM-DD");
    }
    const std::string meal = text_arg(args, "meal");
    if (std::find(kMeals.begin(), kMeals.end(), json(meal)) == kMeals.end()) {
        return refusal("invalid", "the meal is one of the list", json{{"meals", kMeals}});
    }
    const json* lines = lines_arg(args);
    if (lines == nullptr) {
        return refusal("invalid", "lines are 1 to " + std::to_string(kMaxLines) + " objects");
    }
    json entries = json::array();
    std::string names;
    double kcal_total = 0;
    for (std::size_t i = 0; i < lines->size(); ++i) {
        const json& l = (*lines)[i];
        const std::string at = "lines[" + std::to_string(i) + "]: ";
        const std::string name = text_arg(l, "name");
        if (name.empty() || length(name) > 200) {
            return refusal("invalid", at + "the name is 1 to 200 characters");
        }
        const auto kcal = number_arg(l, "kcal", 0, 10000);
        const auto protein = number_arg(l, "protein_g", 0, 1000);
        const auto fat = number_arg(l, "fat_g", 0, 1000);
        const auto carbs = number_arg(l, "carbs_g", 0, 1000);
        if (!kcal || !protein || !fat || !carbs) {
            return refusal("invalid", at + "kcal (0 to 10000), protein_g, fat_g and carbs_g (0 to 1000) are numbers");
        }
        json entry{{"date", date},
                   {"meal", meal},
                   {"name", name},
                   {"kcal", *kcal},
                   {"protein_g", *protein},
                   {"fat_g", *fat},
                   {"carbs_g", *carbs}};
        const auto grams = number_arg(l, "grams", 0.01, 10000);
        if (l.contains("grams") && !l["grams"].is_null() && !grams) {
            return refusal("invalid", at + "grams are above 0 and at most 10000");
        }
        if (grams) {
            entry["grams"] = *grams;
        }
        if (l.contains("item_id") && !l["item_id"].is_null()) {
            const std::string item = text_arg(l, "item_id");
            if (!is_uuid(item)) {
                return refusal("invalid", at + "item_id is the id of one of the person's products");
            }
            // The diary takes a product's numbers from the product and the grams.
            if (!grams) {
                return refusal("invalid", at + "a line with item_id needs its grams");
            }
            entry["item_id"] = item;
        }
        names += (names.empty() ? "" : ", ") + name + (grams ? " " + number_words(*grams) + " g" : "");
        kcal_total += *kcal;
        entries.push_back(std::move(entry));
    }
    // The title, then the day after the last comma: the page shows them as two lines.
    std::string title = meal;
    title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(title[0])));
    return Proposal{"add_food",
                    title + ": " + cut(names, 300) + " (" + number_words(std::round(kcal_total)) + " kcal), " + date,
                    json{{"method", "POST"}, {"path", "/api/v1/food/entries/batch"}, {"body", {{"entries", entries}}}}};
}

/// Posted expenses, in one batch (POST /money/transactions/batch, all or none). Each line's amount
/// is in its account's currency: an account of another currency is refused, never converted.
/// The account is account_id, else the one the merchant was last paid from when it is open and
/// in the currency, else the only open account in the currency; without one the proposal is
/// refused with the accounts to pick from, as the ledger has no row without an account. The
/// category is category_id, else the one remembered for the merchant, else refused with the list.
Proposed add_spend(const Ctx& c, const json& args) {
    static const std::regex kCode("^[A-Z]{3}$");
    const std::string date = day_arg(args, "date").empty() ? c.today : day_arg(args, "date");
    if (date.empty()) {
        return refusal("invalid", "the date is a day as YYYY-MM-DD");
    }
    const json* lines = lines_arg(args);
    if (lines == nullptr) {
        return refusal("invalid", "lines are 1 to " + std::to_string(kMaxLines) + " objects");
    }
    const json archived = json::array({json::array({"archived", "true"})});
    const auto accounts = SiteApi::read_data(c.transport, c.api_url, c.token, "/api/v1/money/accounts", archived);
    const auto categories = SiteApi::read_data(c.transport, c.api_url, c.token, "/api/v1/money/categories", archived);
    const auto currencies =
        SiteApi::read_data(c.transport, c.api_url, c.token, "/api/v1/money/currencies", json::array());
    if (!accounts || !accounts->is_array() || !categories || !categories->is_array() || !currencies ||
        !currencies->is_array()) {
        return refusal("unavailable", "the money accounts could not be read");
    }
    json rows = json::array();
    std::vector<std::string> shown;
    std::vector<std::string> account_names;
    const std::string key = "ani-" + Utils::Crypto::random_hex(8);
    for (std::size_t i = 0; i < lines->size(); ++i) {
        const json& l = (*lines)[i];
        const std::string at = "lines[" + std::to_string(i) + "]: ";
        const auto amount = number_arg(l, "amount", 0, 1e9);
        if (!amount || *amount <= 0) {
            return refusal("invalid", at + "the amount is a number above 0");
        }
        const std::string currency = text_arg(l, "currency");
        if (!std::regex_match(currency, kCode)) {
            return refusal("invalid", at + "the currency is a code like KZT");
        }
        const std::string merchant = text_arg(l, "merchant");
        const std::string name = text_arg(l, "name").empty() ? merchant : text_arg(l, "name");
        if (name.empty() || length(name) > 200 || length(merchant) > 200) {
            return refusal("invalid", at + "the name is 1 to 200 characters, the merchant at most 200");
        }
        json memory = merchant_memory(c, merchant);
        const auto remembered = [&memory](const char* field) {
            return memory.contains(field) && memory[field].is_string() ? memory[field].get<std::string>()
                                                                       : std::string();
        };
        auto picked = line_account(l, at, *accounts, currency, remembered("account_id"));
        if (auto* refused = std::get_if<std::string>(&picked)) {
            return std::move(*refused);
        }
        const json* account = std::get<const json*>(picked);
        std::string category = text_arg(l, "category_id");
        if (category.empty()) {
            category = remembered("category_id");
        }
        const json* kind = by_id(*categories, category);
        if (kind == nullptr || kind->value("kind", "") != "expense") {
            return refusal(category.empty() ? "no_category" : "not_found",
                           at + "say which category: category_id of one of these",
                           json{{"categories", category_choices(*categories)}});
        }
        for (const auto& cur : *currencies) {
            if (cur.is_object() && cur.value("code", "") == currency &&
                !::Money::Currencies::fits(*amount, cur.value("decimals", 2))) {
                return refusal("invalid",
                               at + currency + " has " + std::to_string(cur.value("decimals", 2)) + " decimal(s)");
            }
        }
        rows.push_back(json{{"type", "expense"},
                            {"date", date},
                            {"account_id", account->value("id", "")},
                            {"amount", *amount},
                            {"category_id", category},
                            {"merchant", merchant},
                            {"name", name},
                            {"source", "api"},
                            // A second guard besides the action's own once: the ledger refuses an id it has.
                            {"external_id", key + "-" + std::to_string(i)}});
        shown.push_back(name + " " + number_words(*amount) + " " + currency);
        const std::string account_name = account->value("name", "");
        if (std::find(account_names.begin(), account_names.end(), account_name) == account_names.end()) {
            account_names.push_back(account_name);
        }
    }
    return Proposal{
        "add_spend",
        "Spend: " + cut(joined(shown), 300) + " (" + cut(joined(account_names), 120) + "), " + date,
        json{{"method", "POST"}, {"path", "/api/v1/money/transactions/batch"}, {"body", {{"transactions", rows}}}}};
}

}  // namespace Agent::Actions::detail
