import { useQuery } from '@tanstack/react-query';
import { useState } from 'react';

import { Alert, AlertDescription } from '@/components/ui/alert';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { components } from '@/lib/api/schema.gen';

type Status = components['schemas']['TelegramStatus'];
const PATH = '/api/v1/telegram';

const when = (iso: string) => new Date(iso).toLocaleString();

/**
 * The owner's Telegram for Ani (admin only): link the gateway account to
 * yourself, see when it was last read, unlink it after a confirm.
 */
export function TelegramCard() {
  const status = useQuery({
    queryKey: qk.telegram(),
    queryFn: async ({ signal }) => (await api.getJson<{ data: Status }>(PATH, { signal })).data,
    refetchInterval: 60_000,
  });
  const link = useApiMutation(async () => await api.putJson(PATH), {
    invalidate: [qk.telegram()],
  });
  const unlink = useApiMutation(async () => await api.deleteJson(PATH), {
    invalidate: [qk.telegram()],
  });
  const [sure, setSure] = useState(false);
  const s = status.data;
  const l = s?.link;

  return (
    <Card>
      <CardHeader>
        <CardTitle>Telegram</CardTitle>
        <CardDescription>
          Ani reads your Telegram: she hears about new private messages and @mentions within a
          minute, reacts and marks messages read herself, and proposes messages that you confirm.
        </CardDescription>
      </CardHeader>
      <CardContent className="space-y-3 text-sm">
        {!s ? null : !s.configured ? (
          <p className="text-muted-foreground">
            The server has no gateway token yet (ANI_TELEGRAM_TOKEN in the Secret
            life-os-ani-agent).
          </p>
        ) : s.elsewhere ? (
          <p className="text-muted-foreground">The account is linked to another user.</p>
        ) : !s.linked ? (
          <Button variant="outline" disabled={link.isPending} onClick={() => link.mutate()}>
            Ani reads my Telegram
          </Button>
        ) : (
          <>
            <p>
              Linked{l?.username ? ` to @${l.username}` : ''}.{' '}
              <span className="text-muted-foreground" data-num-ok>
                {l?.polled_at ? `Last read ${when(l.polled_at)}.` : 'Not read yet.'}
              </span>
            </p>
            {l?.last_error && (
              <Alert variant="destructive">
                <AlertDescription>The last read failed: {l.last_error}</AlertDescription>
              </Alert>
            )}
            {sure ? (
              <div className="flex flex-wrap items-center gap-2">
                <span>Ani stops reading your Telegram.</span>
                <Button
                  variant="destructive"
                  size="sm"
                  disabled={unlink.isPending}
                  onClick={() => unlink.mutate(undefined, { onSuccess: () => setSure(false) })}
                >
                  Unlink
                </Button>
                <Button variant="ghost" size="sm" onClick={() => setSure(false)}>
                  Keep
                </Button>
              </div>
            ) : (
              <Button variant="outline" size="sm" onClick={() => setSure(true)}>
                Unlink…
              </Button>
            )}
          </>
        )}
        {(link.error || unlink.error) && (
          <Alert variant="destructive">
            <AlertDescription>{link.error || unlink.error}</AlertDescription>
          </Alert>
        )}
      </CardContent>
    </Card>
  );
}
