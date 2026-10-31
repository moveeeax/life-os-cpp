/**
 * @file VoiceBrief.cpp
 * @brief Body for src/agent/VoiceBrief.hpp.
 */

#include "agent/VoiceBrief.hpp"

#include <exception>

#include <spdlog/spdlog.h>

#include "agent/Manifest.hpp"
#include "now/Voice.hpp"
#include "repositories/agent/BriefRepository.hpp"
#include "repositories/agent/TurnRepository.hpp"
#include "repositories/telegram/LinkRepository.hpp"
#include "telegram/Gateway.hpp"

namespace Agent::VoiceBrief {

namespace {

Counts counts_of(const std::string& owner, const std::string& day, const std::string& tz) {
    const auto n = Repositories::Agent::BriefRepository().day_counts(owner, day, tz);
    return Counts{n[0], n[1], n[2], n[3], n[4]};
}

}  // namespace

std::optional<std::string> moment_of(const std::string& local) {
    if (local.size() < 13) {
        return std::nullopt;
    }
    const std::string hour = local.substr(11, 2);
    if (hour == "08") {
        return std::string("morning");
    }
    if (hour == "21") {
        return std::string("evening");
    }
    return std::nullopt;
}

std::string brief_words(const std::string& kind, const Counts& c) {
    if (kind == "morning") {
        std::string out = "Доброе утро! ";
        out += c.due_today > 0 ? "Задач на сегодня: " + std::to_string(c.due_today) + "." : "На сегодня задач нет.";
        if (c.overdue > 0) {
            out += " Просроченных: " + std::to_string(c.overdue) + ".";
        }
        if (c.habits > 0) {
            out += " Привычек на день: " + std::to_string(c.habits) + ".";
        }
        return out + " Я рядом, если что.";
    }
    std::string out = "Вечер. ";
    out += c.done_today > 0 ? "Сегодня ты закрыл задач: " + std::to_string(c.done_today) + "."
                            : "Сегодня без закрытых задач, бывает.";
    if (c.habits > 0) {
        out += " Привычки: " + std::to_string(c.habits_done) + " из " + std::to_string(c.habits) + ".";
    }
    if (c.due_today > 0) {
        out += " На завтра осталось: " + std::to_string(c.due_today) + ".";
    }
    return out + " Спокойной ночи.";
}

void send_due(Net::Http::Transport& transport, const std::string& owner, const std::string& me_id) {
    try {
        Repositories::Agent::TurnRepository turns;
        const std::string tz = turns.last_tz(owner);
        const std::string local = turns.local_now(tz).value_or("");
        const auto kind = moment_of(local);
        if (!kind) {
            return;
        }
        const auto voice = Now::Voice::read_settings();
        const auto gateway = ::Telegram::settings();
        if (!voice || !gateway) {
            return;
        }
        const std::string day = local.substr(0, 10);
        Repositories::Telegram::LinkRepository links;
        if (!links.claim_voice(owner, *kind, day)) {
            return;
        }
        const std::string words = brief_words(*kind, counts_of(owner, day, tz));
        const std::string audio = Now::Voice::speak(transport, *voice, words, "opus");
        const auto sent = ::Telegram::upload(transport, *gateway, me_id, audio, "voice", "ani.ogg");
        if (!sent.ok()) {
            spdlog::warn("ani agent {}: the {} voice line was not sent (gateway {})",
                         Manifest::owner_hash(owner),
                         *kind,
                         sent.status);
        }
    } catch (const std::exception& e) {
        spdlog::warn("ani agent {}: the voice line failed: {}", Manifest::owner_hash(owner), e.what());
    }
}

}  // namespace Agent::VoiceBrief
