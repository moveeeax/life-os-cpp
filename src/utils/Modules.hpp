/**
 * @file Modules.hpp
 * @brief Feature-module switches + process shutdown flag.
 * @details The tiny slice of Core:: that controllers, guards and job
 *          handlers consult per-request. It lives in utils/ (the bottom
 *          layer) so that a module or a job handler can read its switch
 *          without an edge to core/: core/ is the composition root and
 *          includes the modules, so a module including core/ would close a
 *          cycle. Including a switch costs only utils/Config.hpp. The
 *          namespace stays Core: core/Core.hpp includes this header, so the
 *          names below are usable as Core::* from either include.
 */

#pragma once

#include <atomic>

#include "utils/Config.hpp"
#include "utils/ConfigKeys.hpp"

namespace Core {

/**
 * @brief Shutdown state flag, flipped by main/worker signal handlers before
 *        Core::shutdown() is called. Used by /ready so Kubernetes stops
 *        sending new traffic while in-flight requests complete.
 */
inline std::atomic<bool> shutting_down_flag{false};

inline void begin_shutdown() {
    shutting_down_flag.store(true);
}
inline bool is_shutting_down() {
    return shutting_down_flag.load();
}

/// A feature module's master switch. Every module follows the one pattern:
/// routes are statically registered, handlers consult the switch per-request
/// and 404 while the module is off; before Config is up every switch reads
/// off. The switches below name their registry entry and nothing else.
inline bool module_enabled(const Config::ConfigKey& key) {
    if (!Config::is_initialized())
        return false;
    return Config::get().get<bool>(key, false);
}

inline bool billing_enabled() {
    return module_enabled(Config::Keys::billing_enabled);
}
inline bool fitness_enabled() {
    return module_enabled(Config::Keys::fitness_enabled);
}
inline bool workout_enabled() {
    return module_enabled(Config::Keys::workout_enabled);
}
inline bool food_enabled() {
    return module_enabled(Config::Keys::food_enabled);
}
inline bool money_enabled() {
    return module_enabled(Config::Keys::money_enabled);
}
inline bool tasks_enabled() {
    return module_enabled(Config::Keys::tasks_enabled);
}
inline bool goals_enabled() {
    return module_enabled(Config::Keys::goals_enabled);
}
inline bool now_enabled() {
    return module_enabled(Config::Keys::now_enabled);
}
inline bool travel_enabled() {
    return module_enabled(Config::Keys::travel_enabled);
}
inline bool habits_enabled() {
    return module_enabled(Config::Keys::habits_enabled);
}
inline bool notes_enabled() {
    return module_enabled(Config::Keys::notes_enabled);
}
inline bool practice_enabled() {
    return module_enabled(Config::Keys::practice_enabled);
}
inline bool medical_enabled() {
    return module_enabled(Config::Keys::medical_enabled);
}
inline bool github_enabled() {
    return module_enabled(Config::Keys::github_enabled);
}

}  // namespace Core
