/**
 * @file Dispatcher.cpp
 * @brief Bodies for src/jobs/Dispatcher.hpp — compiled once into app_core.
 *        Registration stays inline in the header (JobHandlerRegistrar runs at
 *        static init from handler headers); the lookup/dispatch paths live
 *        here.
 */

#include "jobs/Dispatcher.hpp"

#include <exception>

#include <spdlog/spdlog.h>

namespace Jobs {

Dispatcher& Dispatcher::get() {
    static Dispatcher instance;
    return instance;
}

std::vector<std::string> Dispatcher::known_types() const {
    std::vector<std::string> out;
    out.reserve(handlers_.size());
    for (const auto& kv : handlers_)
        out.push_back(kv.first);
    return out;
}

std::vector<std::string> Dispatcher::unregistered(const std::vector<std::string>& subscribed) const {
    std::vector<std::string> out;
    for (const auto& t : subscribed)
        if (handlers_.find(t) == handlers_.end())
            out.push_back(t);
    return out;
}

json Dispatcher::dispatch(const Job& job) const {
    auto it = handlers_.find(job.type);
    if (it == handlers_.end())
        throw PermanentJobError("no handler for job type: " + job.type);
    return it->second(job.payload);
}

void Dispatcher::on_dead(const Job& job, const std::string& error) const {
    auto it = dead_hooks_.find(job.type);
    if (it == dead_hooks_.end() || !it->second)
        return;
    try {
        it->second(job.payload, error);
    } catch (const std::exception& e) {
        spdlog::warn("Job {} dead-letter hook for {} failed: {}", job.id, job.type, e.what());
    } catch (...) {
        spdlog::warn("Job {} dead-letter hook for {} failed with a non-std exception", job.id, job.type);
    }
}

}  // namespace Jobs
