import { useState } from 'react';

import { Alert, AlertDescription } from '@/components/ui/alert';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import {
  useApiKeys,
  useCreateApiKey,
  useRevokeApiKey,
  type ApiKey,
  type ApiKeyScope,
} from '@/hooks/useApiKeys';

const day = (iso: string) => new Date(iso).toLocaleDateString();

/** One key: its name and date; scope, prefix, last use and Revoke on click. */
function KeyRow({ k }: { k: ApiKey }) {
  const revoke = useRevokeApiKey();
  const [open, setOpen] = useState(false);
  const [sure, setSure] = useState(false);
  return (
    <li className="py-2">
      <button
        type="button"
        className="flex w-full items-baseline justify-between gap-4 text-left"
        aria-expanded={open}
        onClick={() => setOpen(!open)}
      >
        <span className="truncate">{k.name}</span>
        <span className="shrink-0 text-muted-foreground tabular-nums" data-num-ok>
          {day(k.created_at)}
        </span>
      </button>
      {open && (
        <div className="mt-2 flex flex-wrap items-center gap-x-4 gap-y-2 text-xs text-muted-foreground">
          <span data-num-ok>{k.scope === 'read' ? 'Read only' : 'Full access'}</span>
          <span className="font-mono" data-num-ok>
            {k.prefix}…
          </span>
          <span data-num-ok>
            {k.last_used_at ? `Last used ${day(k.last_used_at)}` : 'Never used'}
          </span>
          {sure ? (
            <span className="flex items-center gap-2">
              <span>Scripts with this key stop working.</span>
              <Button
                variant="destructive"
                size="sm"
                disabled={revoke.isPending}
                onClick={() => revoke.mutate(k.id)}
              >
                Revoke
              </Button>
              <Button variant="ghost" size="sm" onClick={() => setSure(false)}>
                Keep
              </Button>
            </span>
          ) : (
            <Button variant="ghost" size="sm" onClick={() => setSure(true)}>
              Revoke…
            </Button>
          )}
          {revoke.error && <span className="text-destructive">{revoke.error}</span>}
        </div>
      )}
    </li>
  );
}

/** Keys for scripts and agents (the CLI, OpenClaw): made here, shown once, revoked here. */
export function ApiKeysCard() {
  const keys = useApiKeys();
  const create = useCreateApiKey();
  const [name, setName] = useState('');
  const [scope, setScope] = useState<ApiKeyScope>('full');
  const [made, setMade] = useState<string | null>(null);

  return (
    <Card>
      <CardHeader>
        <CardTitle>API keys</CardTitle>
        <CardDescription>
          For scripts and agents: send it as{' '}
          <span className="font-mono">Authorization: Bearer &lt;key&gt;</span>. Ani’s MCP keys are
          in her card.
        </CardDescription>
      </CardHeader>
      <CardContent className="space-y-4">
        {made ? (
          <div className="space-y-2 rounded-md border p-3" role="group" aria-label="New API key">
            <p className="text-sm">Copy it now: the key is not shown again.</p>
            <pre className="num overflow-x-auto rounded-md bg-muted p-2 text-xs">{made}</pre>
            <div className="flex gap-2">
              <Button variant="outline" onClick={() => navigator.clipboard.writeText(made)}>
                Copy
              </Button>
              <Button variant="outline" onClick={() => setMade(null)}>
                Done
              </Button>
            </div>
          </div>
        ) : (
          <form
            className="flex flex-wrap items-center gap-2"
            onSubmit={(e) => {
              e.preventDefault();
              create.mutate(
                { name: name.trim(), scope },
                {
                  onSuccess: (k) => {
                    setMade(k.key);
                    setName('');
                  },
                },
              );
            }}
          >
            <Input
              id="api-key-name"
              aria-label="Key name"
              placeholder="Name, e.g. cli"
              maxLength={80}
              value={name}
              onChange={(e) => setName(e.target.value)}
              className="max-w-xs"
            />
            <select
              id="api-key-scope"
              aria-label="Access"
              value={scope}
              onChange={(e) => setScope(e.target.value as ApiKeyScope)}
              className="h-9 rounded-md border border-input bg-background px-2 text-sm"
            >
              <option value="full">Full access</option>
              <option value="read">Read only</option>
            </select>
            <Button type="submit" variant="outline" disabled={create.isPending || !name.trim()}>
              Create key
            </Button>
          </form>
        )}
        {keys.data && keys.data.length > 0 && (
          <ul className="divide-y text-sm">
            {keys.data.map((k) => (
              <KeyRow key={k.id} k={k} />
            ))}
          </ul>
        )}
        {keys.data?.length === 0 && <p className="text-sm text-muted-foreground">No keys yet.</p>}
        {create.error && (
          <Alert variant="destructive">
            <AlertDescription>{create.error}</AlertDescription>
          </Alert>
        )}
      </CardContent>
    </Card>
  );
}
