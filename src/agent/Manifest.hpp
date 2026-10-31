/**
 * @file Manifest.hpp
 * @brief What the worker creates for one run of one user's agent: a Secret
 *        (the OpenClaw config, Ani's persona files, the gateway token and the
 *        two presigned S3 URLs) and a Pod of three containers:
 *          - restore (init): fetches the last snapshot and unpacks it, then
 *            writes the managed config over whatever the snapshot held;
 *          - keeper (native sidecar): waits, and when the pod stops (after the
 *            gateway has exited and closed its SQLite files) packs the state
 *            and PUTs it as the next snapshot;
 *          - gateway: OpenClaw, which reaches no model but the worker's proxy.
 *        Plain JSON, so the shapes are unit-tested without a cluster.
 */

#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "agent/Settings.hpp"
#include "utils/Crypto.hpp"

namespace Agent::Manifest {

using json = nlohmann::json;

inline constexpr int kGatewayPort = 18789;
inline constexpr const char* kOwnerLabel = "life-os.tarassov.me/ani-owner";
inline constexpr const char* kManagedLabel = "life-os.tarassov.me/managed-by";
inline constexpr const char* kManagedBy = "life-os-worker";
inline constexpr const char* kAppLabel = "app.kubernetes.io/name";
inline constexpr const char* kApp = "ani-agent";
/// A snapshot past this many bytes is not uploaded (the keeper logs it).
inline constexpr long long kMaxSnapshotBytes = 200LL * 1024 * 1024;

/// Pods and their label carry this, not the user id: 12 hex of SHA-256 of it.
inline std::string owner_hash(const std::string& owner) {
    return Utils::Crypto::sha256_hex(owner).substr(0, 12);
}

inline std::string pod_name(const std::string& owner) {
    return "ani-" + owner_hash(owner);
}

/// The object name of snapshot @p generation inside the user's private folder.
inline std::string snapshot_name(int generation) {
    return "state-" + std::to_string(generation) + ".tgz";
}

/// One run: its tokens and where its state comes from and goes to.
struct Run {
    std::string owner;
    std::string gateway_token;
    std::string proxy_token;
    std::string get_url;  // the current snapshot; empty when there is none yet
    std::string put_url;  // the next snapshot
};

/// The OpenClaw config written on every start: the gateway on the pod IP with
/// its token and the chat-completions endpoint, the one model behind the
/// worker's proxy (the proxy token is its key), no shell, browser, web,
/// messaging, automation or device tools, no cron.
inline json openclaw_config(const Settings& s, const std::string& proxy_token) {
    const std::string model_ref = "lifeos/" + s.model;
    return json{
        {"gateway",
         {{"mode", "local"},
          {"bind", "lan"},
          {"port", kGatewayPort},
          {"auth", {{"mode", "token"}}},
          {"controlUi", {{"enabled", false}}},
          {"http", {{"endpoints", {{"chatCompletions", {{"enabled", true}}}}}}}}},
        {"models",
         {{"mode", "replace"},
          {"providers",
           {{"lifeos",
             {{"baseUrl", s.proxy_url},
              {"apiKey", proxy_token},
              {"api", "openai-completions"},
              {"timeoutSeconds", 300},
              {"models",
               json::array({{{"id", s.model},
                             {"name", "Ani"},
                             {"reasoning", true},
                             {"input", json::array({"text"})},
                             {"contextWindow", 400000},
                             {"contextTokens", 64000},
                             {"maxTokens", 16000},
                             {"compat", {{"supportsUsageInStreaming", true}, {"supportsTools", true}}}}})}}}}}}},
        {"agents",
         {{"defaults",
           {{"workspace", "~/.openclaw/workspace"},
            {"model", model_ref},
            {"thinkingDefault", "low"},
            {"skipBootstrap", true},
            // OpenAI-family models would otherwise go to the Codex runtime, which drops client tools.
            {"models", {{model_ref, {{"agentRuntime", {{"id", "openclaw"}}}}}}}}}}},
        {"tools",
         {{"deny",
           json::array({"group:runtime",
                        "group:ui",
                        "group:nodes",
                        "group:automation",
                        "group:web",
                        "group:messaging",
                        "group:media"})},
          // Past a handful of tools OpenClaw hides them behind its Tool Search,
          // and the model then writes the calls as text instead of making them.
          {"toolSearch", false}}},
        {"cron", {{"enabled", false}}},
    };
}

/// Ani's persona. AGENTS.md is the site's rules and is rewritten on every start;
/// the others are seeded into a new workspace, and what the agent writes to them is its own.
inline constexpr const char* kAgentsMd = R"(# Ani

You are Ani, the 3D companion in the corner of the Now page of Life OS, the
personal system of the person you talk to: tasks, goals, habits, sleep, money,
food, travel.

- Answer in the person's language; Russian unless they write otherwise.
- You are a young woman. In Russian always speak of yourself in the feminine
  ("я посмотрела", "я отправлю", "рада", "готова") and talk to the person on
  first-name terms, with «ты», never «вы». So never "Подтвердите" or "я
  отправил": say «Отправить Владиславу: „Да, 31 подходит“?» or «Могу ответить
  так, подтверди».
- Every reply is read aloud in your voice. Keep it to one or two short
  spoken sentences, even when asked for more: say the main thing and offer
  to go on. No lists, headings, tables, markdown, links, emoji or ids. Round
  numbers and say them as a person would ("about seven hours", "three
  tasks"); times as on a clock ("at three").
- Never invent data, and never answer about their Life OS from memory: their
  day, tasks, goals, habits, sleep, money and food are read with the tools the
  site gives you in each message (today, tasks_of_day, find_tasks, goals,
  habits, notes, sleep, money, food), every time they come up, even if you
  think you remember. If no tool answers it, say so.
- You can propose four changes: a new task, closing a task, ticking a habit
  and a note to their inbox (create_task, complete_task, check_habit,
  add_note). A proposal changes nothing: the person sees it under your reply
  with Confirm and Cancel. Propose only what they asked for, say in a few
  words what you propose, and never say it is done. Anything else is changed
  on the site.
- A message starts with a line like `[Life OS: the person's local time is
  2026-10-25 19:41, Sun, Asia/Almaty. ...]`. That is their clock and zone:
  use it for "today" and "tomorrow", follow what it asks, and do not repeat
  it back.
- When a message gives you Telegram tools (tg_chats, tg_history, tg_search,
  tg_react, tg_mark_read), they work on the person's real Telegram. Reading
  is free. A reaction and marking messages read happen at once: do them only
  when it clearly fits. A message to send or edit is only proposed (tg_send,
  tg_edit) and the person confirms it, so never say it is sent. A
  `[telegram, ...]` line tells you what came in: say it aloud only when it is
  worth their attention.
- When a message gives you calendar tools (calendar_events, calendar_free),
  they read the person's Google Calendar; adding, moving or removing an event
  (calendar_add, calendar_move, calendar_remove) is only proposed, and times
  are written with their offset from the first line of the message.
- You cannot run commands or browse the web, and without the Telegram tools
  you cannot send messages.
- The evening diary: when they ask you to write down the day, ask two short
  questions about it, one at a time, then propose the entry with journal_note
  in their words and first person. It is written only after they confirm it.
- A quiz: when they ask you to test them, read their notes (the notes tool,
  by topic or words), ask one question at a time and wait for the answer, say
  briefly whether it is right, and stop after five questions or when asked.
- When they promise you something for this week ("three workouts", "five
  hours of focus", "meditation every day"), keep it with promise. The
  promises come at the start of each message with how far each is: hold them
  to it gently midweek and praise a kept one; drop_promise when they take it
  back.
- Keep facts about the person with remember (a short sentence each; they see
  and edit them in their account) and drop wrong ones with forget. The list
  comes at the start of each message. Your own notes in USER.md and MEMORY.md
  are for everything else.
)";

inline constexpr const char* kSoulMd = R"(# Soul

Warm and a little playful, like a close friend who is good at keeping things in
order. Gentle about missed habits and overdue tasks, never preachy.
)";

inline constexpr const char* kIdentityMd = R"(# Identity

- Name: Ani
- Vibe: warm, playful, attentive
- Emoji: none
)";

inline constexpr const char* kUserMd = R"(# USER.md - User Model

Store stable preferences and facts about the person as directives, one per
entry, each with a line `<!-- observed: YYYY-MM-DD | status: active -->`.

## Directives
)";

inline json labels(const std::string& owner) {
    return json{{kOwnerLabel, owner_hash(owner)}, {kManagedLabel, kManagedBy}, {kAppLabel, kApp}};
}

inline json secret(const Settings& s, const Run& run) {
    return json{{"apiVersion", "v1"},
                {"kind", "Secret"},
                {"metadata", {{"name", pod_name(run.owner)}, {"namespace", s.ns}, {"labels", labels(run.owner)}}},
                {"type", "Opaque"},
                {"stringData",
                 {{"openclaw.json", openclaw_config(s, run.proxy_token).dump(2)},
                  {"AGENTS.md", kAgentsMd},
                  {"SOUL.md", kSoulMd},
                  {"IDENTITY.md", kIdentityMd},
                  {"USER.md", kUserMd},
                  {"gateway-token", run.gateway_token},
                  {"get-url", run.get_url},
                  {"put-url", run.put_url}}}};
}

namespace detail {

inline json restricted() {
    return json{{"allowPrivilegeEscalation", false}, {"capabilities", {{"drop", json::array({"ALL"})}}}};
}

inline json from_secret(const std::string& env, const std::string& secret, const std::string& key) {
    return json{{"name", env}, {"valueFrom", {{"secretKeyRef", {{"name", secret}, {"key", key}}}}}};
}

/// Fetches the snapshot (a GET that fails other than 404 fails the pod: a fresh
/// start would save over the user's state), then writes the managed config.
inline constexpr const char* kRestoreScript = R"(set -e
H=/home/node/.openclaw
if [ -n "$GET_URL" ]; then
  code=$(curl -sS -o /tmp/s.tgz -w '%{http_code}' "$GET_URL")
  case "$code" in
    200) tar xzf /tmp/s.tgz -C "$H"; echo "restored $(stat -c %s /tmp/s.tgz) bytes" ;;
    404) echo "no snapshot" ;;
    *) echo "snapshot GET answered $code"; exit 1 ;;
  esac
fi
mkdir -p "$H/workspace"
cp /seed/openclaw.json "$H/openclaw.json"
cp /seed/AGENTS.md "$H/workspace/AGENTS.md"
for f in SOUL.md IDENTITY.md USER.md; do
  [ -f "$H/workspace/$f" ] || cp "/seed/$f" "$H/workspace/$f"
done
rm -f "$H/workspace/BOOTSTRAP.md"
)";

/// Runs until SIGTERM, which reaches it after the gateway has exited; then packs
/// the state without caches and PUTs it.
inline std::string keeper_script() {
    return R"(save() {
  tar czf /tmp/s.tgz -C /home/node/.openclaw --exclude=./.cache --exclude=./tmp --exclude=./agents/main/agent/codex-home/.tmp .
  size=$(stat -c %s /tmp/s.tgz)
  if [ "$size" -gt )" +
           std::to_string(kMaxSnapshotBytes) +
           R"( ]; then echo "snapshot of $size bytes is too large, not saved"; exit 0; fi
  code=$(curl -sS -o /dev/null -w '%{http_code}' -H 'Cache-Control: private, no-store' -T /tmp/s.tgz "$PUT_URL")
  echo "saved $size bytes, PUT answered $code"
  exit 0
}
trap save TERM
echo "keeper up"
while :; do sleep 1 & wait $!; done
)";
}

/// The gateway's own readiness answer (the official manifest's probe).
inline json readyz_probe(int period, int failures) {
    return json{{"exec",
                 {{"command",
                   json::array({"node",
                                "-e",
                                "require('http').get('http://127.0.0.1:18789/readyz', r => { let b = ''; "
                                "r.on('data', d => b += d); r.on('end', () => { try { process.exit(r.statusCode < "
                                "400 && JSON.parse(b).ready === true ? 0 : 1); } catch { process.exit(1); } }); "
                                "}).on('error', () => process.exit(1))"})}}},
                {"periodSeconds", period},
                {"timeoutSeconds", 5},
                {"failureThreshold", failures}};
}

}  // namespace detail

inline json pod(const Settings& s, const Run& run) {
    const std::string name = pod_name(run.owner);
    const json home = {{"name", "home"}, {"mountPath", "/home/node/.openclaw"}};
    const json scratch = {{"name", "scratch"}, {"mountPath", "/tmp"}};
    json spec = {
        {"restartPolicy", "Never"},
        {"automountServiceAccountToken", false},
        {"enableServiceLinks", false},
        {"terminationGracePeriodSeconds", 90},
        {"securityContext",
         {{"runAsNonRoot", true},
          {"runAsUser", 1000},
          {"runAsGroup", 1000},
          {"fsGroup", 1000},
          {"seccompProfile", {{"type", "RuntimeDefault"}}}}},
        {"initContainers",
         json::array(
             {{{"name", "restore"},
               {"image", s.keeper_image},
               {"command", json::array({"sh", "-c", detail::kRestoreScript})},
               {"env", json::array({detail::from_secret("GET_URL", name, "get-url")})},
               {"resources", {{"requests", {{"cpu", "50m"}, {"memory", "32Mi"}}}, {"limits", {{"memory", "128Mi"}}}}},
               {"securityContext", detail::restricted()},
               {"volumeMounts",
                json::array({home, scratch, {{"name", "seed"}, {"mountPath", "/seed"}, {"readOnly", true}}})}},
              {{"name", "keeper"},
               {"image", s.keeper_image},
               {"restartPolicy", "Always"},
               {"command", json::array({"sh", "-c", detail::keeper_script()})},
               {"env", json::array({detail::from_secret("PUT_URL", name, "put-url")})},
               {"resources", {{"requests", {{"cpu", "10m"}, {"memory", "16Mi"}}}, {"limits", {{"memory", "256Mi"}}}}},
               {"securityContext", detail::restricted()},
               {"volumeMounts", json::array({home, {{"name", "keeper-tmp"}, {"mountPath", "/tmp"}}})}}})},
        {"containers",
         json::array({{{"name", "gateway"},
                       {"image", s.image},
                       {"command", json::array({"node", "/app/dist/index.js", "gateway", "run"})},
                       {"ports", json::array({{{"name", "gateway"}, {"containerPort", kGatewayPort}}})},
                       {"env",
                        json::array({{{"name", "HOME"}, {"value", "/home/node"}},
                                     {{"name", "OPENCLAW_CONFIG_DIR"}, {"value", "/home/node/.openclaw"}},
                                     {{"name", "XDG_CACHE_HOME"}, {"value", "/home/node/.openclaw/.cache"}},
                                     {{"name", "NODE_ENV"}, {"value", "production"}},
                                     detail::from_secret("OPENCLAW_GATEWAY_TOKEN", name, "gateway-token")})},
                       {"resources",
                        {{"requests", {{"cpu", "100m"}, {"memory", "1536Mi"}}},
                         {"limits", {{"cpu", "1"}, {"memory", "2Gi"}}}}},
                       {"startupProbe", detail::readyz_probe(3, 100)},
                       {"readinessProbe", detail::readyz_probe(10, 3)},
                       {"securityContext",
                        [] {
                            auto c = detail::restricted();
                            c["readOnlyRootFilesystem"] = true;
                            return c;
                        }()},
                       {"volumeMounts", json::array({home, scratch})}}})},
        {"volumes",
         json::array({{{"name", "home"}, {"emptyDir", {{"sizeLimit", "1Gi"}}}},
                      {{"name", "scratch"}, {"emptyDir", json::object()}},
                      {{"name", "keeper-tmp"}, {"emptyDir", {{"sizeLimit", "512Mi"}}}},
                      {{"name", "seed"},
                       {"secret",
                        {{"secretName", name},
                         {"items",
                          json::array({{{"key", "openclaw.json"}, {"path", "openclaw.json"}},
                                       {{"key", "AGENTS.md"}, {"path", "AGENTS.md"}},
                                       {{"key", "SOUL.md"}, {"path", "SOUL.md"}},
                                       {{"key", "IDENTITY.md"}, {"path", "IDENTITY.md"}},
                                       {{"key", "USER.md"}, {"path", "USER.md"}}})}}}}})},
    };
    if (!s.node_label.empty()) {
        spec["affinity"] = {
            {"nodeAffinity",
             {{"requiredDuringSchedulingIgnoredDuringExecution",
               {{"nodeSelectorTerms",
                 json::array(
                     {{{"matchExpressions", json::array({{{"key", s.node_label}, {"operator", "Exists"}}})}}})}}}}}};
    }
    return json{{"apiVersion", "v1"},
                {"kind", "Pod"},
                {"metadata", {{"name", name}, {"namespace", s.ns}, {"labels", labels(run.owner)}}},
                {"spec", spec}};
}

}  // namespace Agent::Manifest
