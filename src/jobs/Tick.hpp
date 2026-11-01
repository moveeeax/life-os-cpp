/**
 * @file Tick.hpp
 * @brief Jobs a recurring timer queues (a minute's tick): at most one of a
 *        type is queued or running. With the worker down, or its threads all
 *        held, the old ticks piled up a job a minute per type and ran back to
 *        back once it came back. A marker jobs:tick:<type> stands for the
 *        job; its handler clears it when it ends, and its TTL frees the type
 *        when a job is lost or dead-lettered.
 */

#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace Jobs::Tick {

/// Queues a job of @p type with @p payload unless the last tick's job is
/// still queued or running (the marker lives @p ttl_seconds at most). True
/// when a job was queued. The queue's errors propagate (the marker goes).
bool submit(const std::string& type, const nlohmann::json& payload, long ttl_seconds);

/// The tick's job of @p type ran to its end: the next tick queues again.
/// Not called when the job defers or throws: it comes back by itself.
void done(const std::string& type);

/// The marker's key of @p type.
std::string marker_key(const std::string& type);

}  // namespace Jobs::Tick
