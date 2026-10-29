import { useQuery } from '@tanstack/react-query';
import { useState } from 'react';

import { Alert, AlertDescription } from '@/components/ui/alert';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { components } from '@/lib/api/schema.gen';

type Status = components['schemas']['CalendarStatus'];
const PATH = '/api/v1/calendar';

/** What the consent's return (`/account?calendar=…`) says. */
const OUTCOMES: Record<string, { ok: boolean; text: string }> = {
  ok: { ok: true, text: 'Google Calendar is connected.' },
  denied: { ok: false, text: 'You did not allow access, so nothing was connected.' },
  expired: { ok: false, text: 'The link was too old or already used. Press Connect again.' },
  error: { ok: false, text: 'Google did not finish the connection. Try again in a minute.' },
};

/**
 * The person's Google Calendar: Connect opens Google's consent and comes
 * back to /account?calendar=…; the link shows the account, a broken one
 * asks to connect again, Disconnect revokes after a confirm.
 */
export function CalendarCard({
  go = (url: string) => window.location.assign(url),
  search = window.location.search,
}: {
  go?: (url: string) => void;
  search?: string;
}) {
  const status = useQuery({
    queryKey: qk.calendar(),
    queryFn: async ({ signal }) => (await api.getJson<{ data: Status }>(PATH, { signal })).data,
  });
  const connect = useApiMutation(
    async () => (await api.postJson<{ data: { url: string } }>(`${PATH}/connect`)).data,
    { onSuccess: (d) => go(d.url) },
  );
  const unlink = useApiMutation(async () => await api.deleteJson(PATH), {
    invalidate: [qk.calendar()],
  });
  const [sure, setSure] = useState(false);
  const outcome = OUTCOMES[new URLSearchParams(search).get('calendar') ?? ''];
  const s = status.data;
  const l = s?.link;

  return (
    <Card>
      <CardHeader>
        <CardTitle>Google Calendar</CardTitle>
        <CardDescription>
          Your events on the Now ribbon and in the brief. Ani reads events and free time; she adds,
          moves or removes an event only after you confirm.
        </CardDescription>
      </CardHeader>
      <CardContent className="space-y-3 text-sm">
        {outcome && (
          <Alert variant={outcome.ok ? 'default' : 'destructive'}>
            <AlertDescription>{outcome.text}</AlertDescription>
          </Alert>
        )}
        {!s ? null : !s.configured ? (
          <p className="text-muted-foreground">
            The server has no Google OAuth client yet (GOOGLE_CALENDAR_CLIENT_ID and
            CALENDAR_TOKEN_KEY in the Secret life-os-app).
          </p>
        ) : !l || l.broken ? (
          <>
            {l?.broken && <p>Google refused the access for {l.email}.</p>}
            <Button
              variant="outline"
              disabled={connect.isPending}
              onClick={() => connect.mutate(undefined)}
            >
              {l?.broken ? 'Connect again' : 'Connect Google Calendar'}
            </Button>
          </>
        ) : (
          <>
            <p>Connected to {l.email || 'your Google account'}.</p>
            {sure ? (
              <div className="flex flex-wrap items-center gap-2">
                <span>Life OS forgets the access and Google revokes it.</span>
                <Button
                  variant="destructive"
                  size="sm"
                  disabled={unlink.isPending}
                  onClick={() => unlink.mutate(undefined, { onSuccess: () => setSure(false) })}
                >
                  Disconnect
                </Button>
                <Button variant="ghost" size="sm" onClick={() => setSure(false)}>
                  Keep
                </Button>
              </div>
            ) : (
              <Button variant="outline" size="sm" onClick={() => setSure(true)}>
                Disconnect…
              </Button>
            )}
          </>
        )}
        {(connect.error || unlink.error) && (
          <Alert variant="destructive">
            <AlertDescription>{connect.error || unlink.error}</AlertDescription>
          </Alert>
        )}
      </CardContent>
    </Card>
  );
}
