/**
 * @file Toolset.hpp
 * @brief The tools of one turn of Ani's agent (Turn.cpp): what the gateway is
 *        given and the runner that answers its calls, built from the tool
 *        families the person has (agent/Tools.hpp and the families beside it).
 */

#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "agent/Gateway.hpp"

namespace Agent {

struct Toolset {
    nlohmann::json tools;
    Gateway::ToolRunner run;
};

/// The tools a turn of @p origin offers the user @p owner in the zone @p tz
/// (@p local: their local time "YYYY-MM-DD HH:MM", empty when unknown), with
/// the site's API at @p api_url; the write tools propose under @p turn_id.
/// Without the API or an access token there are no tools, and a call by
/// name is refused as unavailable.
Toolset build_toolset(const std::string& api_url,
                      const std::string& owner,
                      const std::string& tz,
                      const std::string& local,
                      const std::string& turn_id,
                      const std::string& origin);

}  // namespace Agent
