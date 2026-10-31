/**
 * @file DealTools.hpp
 * @brief Promises the person makes to Ani for a week (ani_deals): promise and
 *        drop_promise run at once (the person said it to her; Account lists
 *        them), and each turn starts with the week's deals and their progress
 *        so she can hold the person to them and praise a kept one.
 */

#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "agent/Gateway.hpp"

namespace Agent::DealTools {

nlohmann::json definitions();

bool is_deal(const std::string& name);

/// Runs promise and drop_promise for @p owner in the zone @p tz; the rest goes to @p rest.
Gateway::ToolRunner runner(const std::string& owner, const std::string& tz, Gateway::ToolRunner rest);

/// The line in front of a turn with this week's deals and how far each is; empty without any.
std::string deals_note(const nlohmann::json& deals);

}  // namespace Agent::DealTools
