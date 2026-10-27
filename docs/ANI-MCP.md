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

## Tools

| Tool | What it does |
|---|---|
| `ani_say` | She says a line on Now (1-200 characters). |
| `ani_outfit` | p1, p2, lingerie, or bare (only while bare is on in her settings). |
| `ani_act` | One of her moves: show, greet, peace, shoot, spin, pose, idle_happy, idle_cat, wave. |
| `ani_event` | An event for her agent; she says something about it or stays silent. |
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
