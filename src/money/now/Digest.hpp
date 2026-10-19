/**
 * @file Digest.hpp
 * @brief The money part of the day's digest: today's spending per currency, the spends and the net worth.
 *        The Now screen (GET /api/v1/today, api/TodayController.cpp) reads
 *        each module through one such function and composes nothing itself.
 */

#pragma once

#include <string>

#include <nlohmann/json_fwd.hpp>

namespace Money {

/// {currencies, spends_today, net_worth} of @p owner on @p day; null when all
/// three are empty. Cached in Redis (money/DigestCache.hpp): a money write
/// of the owner or a new day of rates makes the next call compute afresh.
nlohmann::json digest(const std::string& owner, const std::string& day);

}  // namespace Money
