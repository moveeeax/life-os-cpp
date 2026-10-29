/**
 * @file FactTools.hpp
 * @brief What Ani keeps about the person, in Life OS (ani_facts): remember and
 *        forget run at once (the person sees and edits the list in Account),
 *        and each turn starts with the list so she does not lean on her own
 *        notes for it.
 */

#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "agent/Gateway.hpp"

namespace Agent::FactTools {

nlohmann::json definitions();

bool is_fact(const std::string& name);

/// Runs remember and forget for @p owner; the rest goes to @p rest.
Gateway::ToolRunner runner(const std::string& owner, Gateway::ToolRunner rest);

/// The line in front of a turn with the person's facts; empty without any.
std::string facts_note(const nlohmann::json& facts);

}  // namespace Agent::FactTools
