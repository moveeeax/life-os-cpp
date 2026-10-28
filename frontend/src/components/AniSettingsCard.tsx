import { useEffect, useState } from 'react';

import { Alert, AlertDescription } from '@/components/ui/alert';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import {
  useAniPrefs,
  useAniMcpKeys,
  useAniWebhook,
  useCreateAniMcpKey,
  useRevokeAniMcpKey,
  useRotateAniWebhook,
  useSaveAniPrefs,
  type AniWebhook,
} from '@/hooks/useAni';

const SAY = `curl -X POST "$URL" -H 'Content-Type: application/json' \\
  -d '{"type":"say","text":"Сборка прошла."}'`;
const EVENT = `curl -X POST "$URL" -H 'Content-Type: application/json' \\
  -d '{"type":"event","source":"telegram","text":"Маша: созвон через 10 минут?"}'`;

const when = (iso: string) => new Date(iso).toLocaleString();

/** The bare outfit: off by default; on only after the user says they are 18+. */
function BareSwitch() {
  const prefs = useAniPrefs();
  const save = useSaveAniPrefs();
  const [asking, setAsking] = useState(false);
  const on = prefs.data?.bare ?? false;

  return (
    <div className="space-y-3">
      <div className="flex items-center justify-between gap-4">
        <div className="text-sm">
          <div id="ani-bare-label" className="font-medium">
            Bare outfit
          </div>
          <div className="text-muted-foreground">
            Adds Bare to her wardrobe on Now. Hidden while Hide numbers is on.
          </div>
        </div>
        <button
          type="button"
          role="switch"
          aria-checked={on}
          aria-labelledby="ani-bare-label"
          disabled={!prefs.isSuccess || save.isPending}
          onClick={() => (on ? save.mutate(false) : setAsking(true))}
          className={`relative h-6 w-11 shrink-0 rounded-full border transition-colors disabled:opacity-50 ${
            on ? 'bg-primary' : 'bg-muted'
          }`}
        >
          <span
            className={`absolute top-0.5 size-4.5 rounded-full bg-background shadow transition-transform ${
              on ? 'translate-x-5.5' : 'translate-x-0.5'
            }`}
          />
        </button>
      </div>
      {asking && (
        <div className="space-y-3 rounded-md border p-4" role="group" aria-label="Turn on Bare">
          <p className="text-sm">
            Only for adults. The model shows her without clothes. Are you 18 or older?
          </p>
          <div className="flex flex-wrap gap-3">
            <Button
              disabled={save.isPending}
              onClick={() => save.mutate(true, { onSuccess: () => setAsking(false) })}
            >
              I am 18 or older
            </Button>
            <Button variant="outline" onClick={() => setAsking(false)}>
              Cancel
            </Button>
          </div>
        </div>
      )}
      {save.error && (
        <Alert variant="destructive">
          <AlertDescription>{save.error}</AlertDescription>
        </Alert>
      )}
    </div>
  );
}

/** The URL with Copy and Replace, examples, and the last calls (no texts). */
function Webhook({ hook }: { hook: AniWebhook }) {
  const rotate = useRotateAniWebhook();
  const [copied, setCopied] = useState(false);
  const [replacing, setReplacing] = useState(false);
  useEffect(() => {
    if (!copied) return;
    const t = setTimeout(() => setCopied(false), 2000);
    return () => clearTimeout(t);
  }, [copied]);

  return (
    <div className="space-y-4">
      <div className="space-y-2">
        <label htmlFor="ani-hook-url" className="text-sm font-medium">
          Webhook URL
        </label>
        <div className="flex gap-2">
          <Input
            id="ani-hook-url"
            readOnly
            value={hook.url}
            onFocus={(e) => e.currentTarget.select()}
            className="num font-mono text-xs"
          />
          <Button
            variant="outline"
            onClick={() => navigator.clipboard.writeText(hook.url).then(() => setCopied(true))}
          >
            {copied ? 'Copied' : 'Copy'}
          </Button>
        </div>
        <p className="text-xs text-muted-foreground">
          Anyone with this URL can make her talk. Keep it like a password.
        </p>
      </div>

      {replacing ? (
        <div className="space-y-3 rounded-md border p-4" role="group" aria-label="Replace URL">
          <p className="text-sm">Replace the URL? The old one stops working at once.</p>
          <div className="flex flex-wrap gap-3">
            <Button
              variant="destructive"
              disabled={rotate.isPending}
              onClick={() => rotate.mutate(undefined, { onSuccess: () => setReplacing(false) })}
            >
              Replace it
            </Button>
            <Button variant="outline" onClick={() => setReplacing(false)}>
              Keep it
            </Button>
          </div>
        </div>
      ) : (
        <Button variant="outline" onClick={() => setReplacing(true)}>
          Replace URL
        </Button>
      )}

      <details className="text-sm">
        <summary className="cursor-pointer text-muted-foreground">Examples</summary>
        <div className="mt-2 space-y-2">
          <p>She says a line:</p>
          <pre className="overflow-x-auto rounded-md bg-muted p-2 text-xs">{SAY}</pre>
          <p>An event for her agent; she decides what to tell you, or stays silent:</p>
          <pre className="overflow-x-auto rounded-md bg-muted p-2 text-xs">{EVENT}</pre>
          <p>
            Also: {'{"type":"outfit","outfit":"p2"}'}, {'{"type":"act","name":"wave"}'},{' '}
            {'{"type":"remind","text":"Встань","in_seconds":1500}'},{' '}
            {'{"type":"ask","text":"Подтвердить?","callback":"https://…"}'},{' '}
            {'{"type":"card","title":"CI","text":"зеленый"}'}, mood, focus, wallpaper, sound and
            hold. A Telegram bot or an n8n flow can forward a message as an event. Items wait for
            your Now page for a day.
          </p>
        </div>
      </details>

      {hook.recent.length > 0 && (
        <div className="space-y-1">
          <div className="text-sm font-medium">Last calls</div>
          <ul className="divide-y text-sm">
            {hook.recent.map((r) => (
              <li key={r.id} className="flex justify-between gap-4 py-1.5">
                <span>
                  {r.kind}, <span className="text-muted-foreground">{r.status}</span>
                </span>
                <span className="text-muted-foreground tabular-nums" data-num-ok>
                  {when(r.created_at)}
                </span>
              </li>
            ))}
          </ul>
        </div>
      )}
      {rotate.error && (
        <Alert variant="destructive">
          <AlertDescription>{rotate.error}</AlertDescription>
        </Alert>
      )}
    </div>
  );
}

/** Her MCP: the endpoint, a key made here (shown once) and the keys to revoke. */
function Mcp() {
  const keys = useAniMcpKeys();
  const create = useCreateAniMcpKey();
  const revoke = useRevokeAniMcpKey();
  const [made, setMade] = useState<string | null>(null);
  const endpoint = `${window.location.origin}/api/v1/ani/mcp`;
  const command = made
    ? `claude mcp add --transport http ani ${endpoint} --header "Authorization: Bearer ${made}"`
    : '';

  return (
    <div className="space-y-3">
      <div>
        <div className="text-sm font-medium">MCP</div>
        <p className="text-xs text-muted-foreground">
          An AI client (Claude Code, Cursor, OpenClaw, Grok) can drive her and chat with her agent
          at <span className="font-mono">{endpoint}</span> with a key made here. The key works only
          for Ani.
        </p>
      </div>
      {made ? (
        <div className="space-y-2 rounded-md border p-3" role="group" aria-label="New MCP key">
          <p className="text-sm">Copy it now: the key is not shown again.</p>
          <pre className="num overflow-x-auto rounded-md bg-muted p-2 text-xs">{command}</pre>
          <div className="flex gap-2">
            <Button variant="outline" onClick={() => navigator.clipboard.writeText(command)}>
              Copy
            </Button>
            <Button variant="outline" onClick={() => setMade(null)}>
              Done
            </Button>
          </div>
        </div>
      ) : (
        <Button
          variant="outline"
          disabled={create.isPending}
          onClick={() => create.mutate(undefined, { onSuccess: (k) => setMade(k.key) })}
        >
          Create key
        </Button>
      )}
      {keys.data && keys.data.length > 0 && (
        <ul className="divide-y text-sm">
          {keys.data.map((k) => (
            <li key={k.id} className="flex items-center justify-between gap-4 py-1.5">
              <span className="font-mono text-xs" data-num-ok>
                {k.prefix}
              </span>
              <span className="text-muted-foreground tabular-nums" data-num-ok>
                {when(k.created_at)}
              </span>
              <Button
                variant="ghost"
                size="sm"
                aria-label={`Revoke ${k.prefix}`}
                disabled={revoke.isPending}
                onClick={() => revoke.mutate(k.id)}
              >
                Revoke
              </Button>
            </li>
          ))}
        </ul>
      )}
      {(create.error || revoke.error) && (
        <Alert variant="destructive">
          <AlertDescription>{create.error || revoke.error}</AlertDescription>
        </Alert>
      )}
    </div>
  );
}

export function AniSettingsCard() {
  const hook = useAniWebhook();
  return (
    <Card>
      <CardHeader>
        <CardTitle>Ani</CardTitle>
        <CardDescription>
          Her wardrobe, a webhook other systems use to talk to you through her, and her MCP.
        </CardDescription>
      </CardHeader>
      <CardContent className="space-y-6">
        <BareSwitch />
        {hook.data && <Webhook hook={hook.data} />}
        <Mcp />
      </CardContent>
    </Card>
  );
}
