import { useQuery } from '@tanstack/react-query';
import { useState } from 'react';

import { Alert, AlertDescription } from '@/components/ui/alert';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import type { components } from '@/lib/api/schema.gen';

type Fact = components['schemas']['AniFact'];
type Prefs = components['schemas']['AniPrefs'];

const KEY = ['ani', 'facts'] as const;
const PREFS = ['ani', 'prefs'] as const;

/** Her mutable kinds of lines, as the page names them. */
export const LINE_KINDS: Record<string, string> = {
  brief: 'Morning brief',
  evening: 'Evening word',
  break: 'Break after focus',
  stretch: 'Stretch reminders',
  hello: 'Hellos',
  telegram: 'Telegram news',
  webhook: 'Webhook events',
  reminder: 'Reminders',
  calendar: 'Calendar news',
};

function FactRow({ fact }: { fact: Fact }) {
  const [text, setText] = useState<string | null>(null);
  const save = useApiMutation(
    async (t: string) => await api.patchJson(`/api/v1/ani/facts/${fact.id}`, { body: { text: t } }),
    { invalidate: [KEY], onSuccess: () => setText(null) },
  );
  const remove = useApiMutation(async () => await api.deleteJson(`/api/v1/ani/facts/${fact.id}`), {
    invalidate: [KEY],
  });
  return (
    <li className="flex items-center gap-2 py-1.5">
      {text === null ? (
        <>
          <span className="flex-1">{fact.text}</span>
          <span className="text-xs text-muted-foreground">
            {fact.source === 'chat' ? 'from a chat' : 'yours'}
          </span>
          <Button variant="ghost" size="sm" onClick={() => setText(fact.text)}>
            Edit
          </Button>
          <Button
            variant="ghost"
            size="sm"
            disabled={remove.isPending}
            onClick={() => remove.mutate()}
          >
            Delete
          </Button>
        </>
      ) : (
        <>
          <Input
            aria-label="The fact"
            value={text}
            maxLength={200}
            onChange={(e) => setText(e.target.value)}
          />
          <Button
            variant="outline"
            size="sm"
            disabled={save.isPending || text.trim() === ''}
            onClick={() => save.mutate(text.trim())}
          >
            Save
          </Button>
          <Button variant="ghost" size="sm" onClick={() => setText(null)}>
            Cancel
          </Button>
        </>
      )}
    </li>
  );
}

/** What Ani keeps about the user, and the kinds of her lines they muted on Now. */
export function AniFacts() {
  const facts = useQuery({
    queryKey: KEY,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: Fact[] }>('/api/v1/ani/facts', { signal })).data,
  });
  const prefs = useQuery({
    queryKey: PREFS,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: Prefs }>('/api/v1/ani/prefs', { signal })).data,
  });
  const add = useApiMutation(
    async (t: string) => await api.postJson('/api/v1/ani/facts', { body: { text: t } }),
    { invalidate: [KEY], onSuccess: () => setDraft('') },
  );
  const unmute = useApiMutation(
    async (kind: string) =>
      await api.putJson('/api/v1/ani/prefs', {
        body: { muted: (prefs.data?.muted ?? []).filter((k) => k !== kind) },
      }),
    { invalidate: [PREFS] },
  );
  const [open, setOpen] = useState(false);
  const [draft, setDraft] = useState('');
  const list = Array.isArray(facts.data) ? facts.data : [];
  const muted = Array.isArray(prefs.data?.muted) ? prefs.data.muted : [];
  return (
    <div className="space-y-3">
      <button
        type="button"
        className="text-sm font-medium"
        aria-expanded={open}
        onClick={() => setOpen(!open)}
      >
        What Ani knows about you ({list.length})
      </button>
      {open && (
        <>
          <p className="text-xs text-muted-foreground">
            She keeps these with remember and reads them before every answer. Change or delete any.
          </p>
          {list.length > 0 && (
            <ul className="divide-y text-sm">
              {list.map((f) => (
                <FactRow key={f.id} fact={f} />
              ))}
            </ul>
          )}
          <form
            className="flex gap-2"
            onSubmit={(e) => {
              e.preventDefault();
              if (draft.trim()) add.mutate(draft.trim());
            }}
          >
            <Input
              aria-label="A new fact"
              placeholder="Something she should know"
              value={draft}
              maxLength={200}
              onChange={(e) => setDraft(e.target.value)}
            />
            <Button type="submit" variant="outline" disabled={add.isPending || draft.trim() === ''}>
              Add
            </Button>
          </form>
          {muted.length > 0 && (
            <div className="space-y-1">
              <div className="text-sm font-medium">Muted on Now</div>
              <ul className="text-sm">
                {muted.map((k) => (
                  <li key={k} className="flex items-center justify-between py-1">
                    <span>{LINE_KINDS[k] ?? k}</span>
                    <Button
                      variant="ghost"
                      size="sm"
                      disabled={unmute.isPending}
                      onClick={() => unmute.mutate(k)}
                    >
                      Turn back on
                    </Button>
                  </li>
                ))}
              </ul>
            </div>
          )}
          {(add.error || unmute.error) && (
            <Alert variant="destructive">
              <AlertDescription>{add.error || unmute.error}</AlertDescription>
            </Alert>
          )}
        </>
      )}
    </div>
  );
}
