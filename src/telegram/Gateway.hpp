/**
 * @file Gateway.hpp
 * @brief The owner's telegram-rest-gateway as Life OS reaches it: its REST
 *        base and the client token of the owner's account, both server
 *        config (ani.telegram.*). The token never leaves the server: Ani's
 *        agent calls the site's own /api/v1/telegram routes, which call the
 *        gateway here. No token, no Telegram anywhere.
 */

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "net/Http.hpp"

namespace Telegram {

struct Settings {
    /// The REST base without a trailing slash, e.g. https://tg.tarassov.me.
    std::string url;
    std::string token;
};

/// The settings; nullopt when no token is configured.
std::optional<Settings> settings();

/// What the gateway answered: the HTTP status and the parsed body (null when
/// it was not JSON). status 0: no answer arrived.
struct Reply {
    long status = 0;
    nlohmann::json body;
    bool ok() const { return status >= 200 && status < 300; }
    /// The `data` of an {ok, data} answer, or the body itself.
    nlohmann::json data() const;
};

using Query = std::vector<std::pair<std::string, std::string>>;

/// One call: @p method on @p path (from /v1/...) with @p query and an
/// optional JSON @p body; @p idempotency_key goes in Idempotency-Key.
Reply call(Net::Http::Transport& transport,
           const Settings& s,
           const std::string& method,
           const std::string& path,
           const Query& query = {},
           const nlohmann::json& body = nullptr,
           const std::string& idempotency_key = "");

/// Sends @p bytes as a file of @p type (voice, audio, document...) to @p chat, named @p file_name, with
/// an optional @p caption: POST /v1/chats/{chat}/files with the raw body.
Reply upload(Net::Http::Transport& transport,
             const Settings& s,
             const std::string& chat,
             const std::string& bytes,
             const std::string& type,
             const std::string& file_name,
             const std::string& caption = "");

/// Whether @p id is a Telegram chat or message id as the gateway writes it
/// (an optional minus and 1 to 20 digits).
bool valid_id(const std::string& id);

/// What a gateway message says: its text, or the caption of a photo, a file
/// and the like; "[voice]" and such for media without words.
std::string text_of(const nlohmann::json& message);

/// @p s percent-encoded for a query value.
std::string encode(const std::string& s);

}  // namespace Telegram
