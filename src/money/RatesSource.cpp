#include "money/RatesSource.hpp"

#include <cctype>
#include <string>

#include <nlohmann/json.hpp>

namespace Money::RatesSource {

namespace {

constexpr const char* kUserAgent = "LifeOS/1.11 (https://life-os.tarassov.me)";

}  // namespace

Day fetch(Net::Http::Transport& transport, const std::string& date_or_latest) {
    Net::Http::Response response;
    try {
        response = transport.get(url_for(date_or_latest), {{"User-Agent", kUserAgent}, {"Accept", "application/json"}});
    } catch (const Net::Http::TransportError& e) {
        throw Unavailable(e.what());
    }
    if (response.status == 404 && date_or_latest != "latest") {
        throw NoSnapshot(date_or_latest);
    }
    if (response.status != 200) {
        throw Unavailable("rates source answered HTTP " + std::to_string(response.status));
    }
    const nlohmann::json body = nlohmann::json::parse(response.body, nullptr, /*allow_exceptions=*/false);
    if (body.is_discarded() || !body.is_object() || !body.contains("usd") || !body["usd"].is_object()) {
        throw Unavailable("rates source answered something that is not the usd table");
    }
    Day day;
    day.date = body.value("date", date_or_latest);
    for (const auto& [code, value] : body["usd"].items()) {
        // Only three-letter codes: the table also lists crypto and metals by longer names.
        if (code.size() != 3 || !value.is_number() || value.get<double>() <= 0) {
            continue;
        }
        std::string upper;
        for (const char c : code) {
            if (!std::isalpha(static_cast<unsigned char>(c))) {
                upper.clear();
                break;
            }
            upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
        if (!upper.empty()) {
            day.per_usd[upper] = value.get<double>();
        }
    }
    if (day.per_usd.empty()) {
        throw Unavailable("rates source answered an empty table");
    }
    return day;
}

}  // namespace Money::RatesSource
