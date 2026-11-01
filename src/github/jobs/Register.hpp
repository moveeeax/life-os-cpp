/**
 * @file Register.hpp
 * @brief The GitHub job handlers (core/JobHandlers.cpp calls this once).
 */

#pragma once

namespace Jobs {
class Dispatcher;
}

namespace Github {

void register_jobs(Jobs::Dispatcher& d);

}  // namespace Github
