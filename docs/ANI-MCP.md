# Ani's MCP

Any AI client that speaks the Model Context Protocol can drive Ani on your
Now page and chat with her agent.

## Endpoint and key

- Endpoint: `https://<your site>/api/v1/ani/mcp` (Streamable HTTP, one
  JSON-RPC request per POST, plain JSON answers; `GET` is 405).
- Key: make one in Account → Ani → MCP → Create key. It has the scope
  `ani` and works only on this endpoint (403 `key_scope` anywhere else).
  It is shown once. A `full` key works too; a `read` key does not.
- Send it as `Authorization: Bearer cpk_…`.
- Protocol versions: `2026-07-28` (stateless, `server/discover`) and
  `2025-11-25`, `2025-06-18` (`initialize`).

The other two scopes are the site's, not Ani's: a `read` or `full` key
from Account → API keys works on the data routes (`X-API-Key`, or as the
Bearer token) with its owner's rights, and a `read` key gets 403
`key_read_only` on anything but GET. The GitHub section follows that rule
and adds one of its own. `GET /api/v1/github`, `GET /api/v1/github/repos`
and `GET /api/v1/github/repos/{owner}/{name}` take a `read` key (the skills
pack reads the list this way). Linking and unlinking
(`/api/v1/github/oauth/start`, `DELETE /api/v1/github`), Sync now, the
mutes and "Make a task" answer 401 `key_not_allowed` to any key, `full`
included: they change the link or act for the person, and only a signed-in
session may.

## Tools

| Tool | What it does |
|---|---|
| `ani_say` | She says a line on Now (1-200 characters). |
| `ani_outfit` | p1, p2, lingerie, or bare (only while bare is on in her settings). |
| `ani_act` | One of her moves: show, greet, peace, shoot, spin, pose, idle_happy, idle_cat, wave. |
| `ani_event` | An event for her agent; she says something about it or stays silent. |
| `ani_mood` | Her face takes a mood for a while: -1, 0 or 1 (`seconds` 60-3600, default 600). |
| `ani_remind` | A line said later: `in_seconds` (60 to a week) or `at` (ISO-8601 with a zone). |
| `ani_ask` | A question with answer buttons on Now (default Да / Нет, up to 4); read the answer with `ani_item`, or give an https `callback` to get it POSTed. |
| `ani_focus` | Starts (`minutes` 1-180) or stops the focus timer on Now. |
| `ani_card` | A small card over her ledge for a few seconds (`title`, `text`, https `url`), not spoken. |
| `ani_wallpaper` / `ani_sound` | Switch the wallpaper (an id, or the next one) or the focus sound (an id, or `off`); `list: true` returns the ids (wallpapers per `device`: desktop or phone). |
| `ani_hold` | Holds everything back from the page for 0-480 minutes (0 ends it). |
| `ani_task` / `ani_habit` / `ani_note` | Proposes a task, a habit tick or a note; the user confirms it on Now. `ani_reply` with the turn_id shows the proposal. |
| `ani_reminders` / `ani_reminder_add` / `ani_reminder_remove` | Recurring reminders: `text`, `time` (HH:MM, local), `days` (1-7, Monday = 1; default every day), `tz` (default the user's); at most 20. |
| `ani_presence` | Whether the Now page is open (from the page's polls; a tab in the background may read as away). |
| `ani_items` / `ani_item` | Items sent to her (MCP and webhook) and what became of them, with her reply to events. |
| `ani_chat` | A message to her agent, as in the chat on Now; returns a `turn_id`. Works with the page closed. |
| `ani_reply` | The answer to a chat turn; `wait_seconds` (up to 25) waits for it. |
| `ani_history` | The latest messages of her chat, from every source. |

The four commands share the webhook's rules: they wait for the open page
(a day by default, `ttl` up to a week), an `id` is taken once in 24 hours,
120 commands an hour together with the webhook, at most 50 waiting. Chat
messages count in the chat's own limit (60 an hour). A short chat reply is
also said aloud on an open Now page. Changes her agent proposes (a task, a
habit) still wait for your confirmation on Now.

## Answers to a question

With a `callback`, the answer to `ani_ask` (or the webhook's `ask`) is
POSTed there once, as `{id, source_id, question, answer}`; a question that
ran out unanswered sends `answer: null, reason: "expired"` within the hour.
The body is signed: `X-Webhook-Signature: sha256=<HMAC-SHA256 of the body,
keyed with your webhook token>`. The token is the part of your webhook
URL after `/hook/` (Account → Ani → Webhook URL); replacing the URL
changes it. The callback must be a public https address.

## Connecting a client

Claude Code:

```sh
claude mcp add --transport http ani https://<your site>/api/v1/ani/mcp \
  --header "Authorization: Bearer cpk_…"
```

Cursor (`~/.cursor/mcp.json`):

```json
{
  "mcpServers": {
    "ani": {
      "url": "https://<your site>/api/v1/ani/mcp",
      "headers": { "Authorization": "Bearer ${env:ANI_MCP_KEY}" }
    }
  }
}
```

OpenClaw (2026.5.12 or newer: earlier versions could send custom headers
to another site on a redirect, CVE-2026-53840):

```json
{
  "mcp": {
    "servers": {
      "ani": {
        "url": "https://<your site>/api/v1/ani/mcp",
        "transport": "streamable-http",
        "headers": { "Authorization": "Bearer ${ANI_MCP_KEY}" }
      }
    }
  }
}
```

xAI API (Responses API, remote MCP tool):

```json
{ "type": "mcp", "server_url": "https://<your site>/api/v1/ani/mcp",
  "server_label": "ani", "authorization": "cpk_…" }
```

Claude Desktop has no direct HTTP transport; use the `mcp-remote` bridge
with `--header "Authorization: Bearer cpk_…"`. The web connectors of
claude.ai and ChatGPT accept OAuth only and are not supported.

## Audit

What comes through the webhook and the MCP is recorded in the admin audit
log (Admin → Audit): `ani.inbox.<kind>` with how it came (`via`), answers to
questions (`ani.ask.answer`), and reminders added, removed and said
(`ani.reminder.*`). Texts are never recorded. Recurring reminders are managed
through the API (`/api/v1/ani/reminders`) and the MCP only.

