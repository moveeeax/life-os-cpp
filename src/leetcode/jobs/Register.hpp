/**
 * @file Register.hpp
 * @brief The LeetCode job handlers (core/JobHandlers.cpp calls this once).
 */

#pragma once

namespace Jobs {
class Dispatcher;
}

namespace LeetCode {

void register_jobs(Jobs::Dispatcher& d);

}  // namespace LeetCode
