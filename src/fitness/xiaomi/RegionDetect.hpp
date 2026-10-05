/**
 * @file RegionDetect.hpp
 * @brief Find the cloud region that holds an account's data.
 *
 * An account's data lives in one region. A request to another region is not
 * an error: it answers with an empty list (checked 2026-10-05 on a real
 * account: six regions answered code 0, one of them with data). So the region
 * is the candidate that returns something.
 */

#pragma once

#include <array>
#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "fitness/xiaomi/CloudClient.hpp"
#include "fitness/xiaomi/Errors.hpp"
#include "fitness/xiaomi/Regions.hpp"

namespace Xiaomi {

/// Candidates in the order they are asked: cn first, it is the region of the
/// accounts seen so far.
inline constexpr std::array<std::string_view, 6> kRegionProbeOrder = {"cn", "sg", "de", "ru", "us", "i2"};

/// How far back the probe looks for data.
inline constexpr int kRegionProbeDays = 30;

/**
 * @brief The first region that returns step records for the last
 *        kRegionProbeDays days, or nullopt when none does (a new account).
 * @param client a client after login()
 * @param today  YYYY-MM-DD, the end of the probed range
 * @throws MiFitnessAuthError when Xiaomi refuses the session. A region whose
 *         request fails otherwise is skipped.
 */
inline std::optional<std::string> detect_region(CloudClient& client, std::string_view today) {
    const std::chrono::sys_days end = detail::parse_date(today);
    const std::chrono::year_month_day start_ymd{end - std::chrono::days{kRegionProbeDays - 1}};
    char start[16];
    std::snprintf(start,
                  sizeof(start),
                  "%04d-%02u-%02u",
                  static_cast<int>(start_ymd.year()),
                  static_cast<unsigned>(start_ymd.month()),
                  static_cast<unsigned>(start_ymd.day()));

    for (const std::string_view region : kRegionProbeOrder) {
        try {
            // One page is enough: the question is whether anything is there.
            const auto [start_time, end_time] = range_to_timestamps(start, today, region);
            const nlohmann::json result = client.post_signed(
                host_for_region(region),
                "/app/v1/data/get_fitness_data_by_time",
                nlohmann::json{{"start_time", start_time}, {"end_time", end_time}, {"key", "steps"}});
            if (result.contains("data_list") && result["data_list"].is_array() && !result["data_list"].empty()) {
                return std::string(region);
            }
        } catch (const MiFitnessAuthError&) {
            throw;
        } catch (const MiFitnessProtocolError& e) {
            spdlog::warn("region probe: {} did not answer: {}", region, e.what());
        }
    }
    return std::nullopt;
}

}  // namespace Xiaomi
