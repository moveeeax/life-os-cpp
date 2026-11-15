/**
 * @file SiteApi.hpp
 * @brief The call every tool family of Ani's agent makes to the site's own
 *        API as the user: a request {method, path, query: [[k, v]...], body}
 *        sent with the turn's access token, and what the model reads of the
 *        answer. One place for the headers, the timeout and the outage text.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "net/Http.hpp"

namespace Agent::SiteApi {

/// How long one call to the site's API may take.
inline constexpr long kTimeoutSeconds = 20;

/// @p api_url + @p path with the pairs of @p query ([[k, v]...]) percent-encoded.
std::string url_of(const std::string& api_url, const std::string& path, const nlohmann::json& query);

/// The HTTP request of @p req ({method (GET when absent), path, query, body}) as the user.
Net::Http::Request request_of(const std::string& api_url, const std::string& token, const nlohmann::json& req);

/// The data of a GET of @p path as the user; nullopt when the API refused it.
/// @throws Net::Http::TransportError when no answer came.
std::optional<nlohmann::json> read_data(Net::Http::Transport& transport,
                                        const std::string& api_url,
                                        const std::string& token,
                                        const std::string& path,
                                        nlohmann::json query);

/// What the model reads of the call @p req of the tool @p tool: with
/// @p keep_ids the data of a 200 as it came (ids stay: she acts by them),
/// else Tools::result_text; an outage is the "unavailable" refusal.
std::string tool_result(Net::Http::Transport& transport,
                        const std::string& api_url,
                        const std::string& token,
                        const nlohmann::json& req,
                        bool keep_ids,
                        const std::string& tool);

}  // namespace Agent::SiteApi
