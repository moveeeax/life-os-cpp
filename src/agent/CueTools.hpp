/**
 * @file CueTools.hpp
 * @brief Ani's own cue on a screen of the app (ani_cues, agent/Cues.hpp):
 *        set_cue and clear_cue run at once. A cue is one line with an
 *        optional button, for today in the person's zone, one per screen;
 *        GET /api/v1/ani/cues shows it after the rules' cues until the day
 *        ends or the person sends it away.
 */

#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "agent/Gateway.hpp"

namespace Agent::CueTools {

nlohmann::json definitions();

bool is_cue(const std::string& name);

/// Runs set_cue and clear_cue for @p owner in the zone @p tz; the rest goes to @p rest.
Gateway::ToolRunner runner(const std::string& owner, const std::string& tz, Gateway::ToolRunner rest);

}  // namespace Agent::CueTools
