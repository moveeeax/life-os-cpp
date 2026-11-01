import { useEffect, useState } from 'react';
import { Link } from 'react-router';

import { Alert, AlertDescription } from '@/components/ui/alert';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { OAUTH_START, useGithubStatus, useGithubSync, useGithubUnlink } from '@/hooks/useGithub';
import { ApiClientError } from '@/lib/api/client';
import { orgHintLine, syncRetryAt, syncedAgo } from '@/lib/github';

/** What the authorization's return (`/account?github=…`) says. */
const OUTCOMES: Record<string, { ok: boolean; text: string }> = {
  ok: { ok: true, text: 'GitHub is connected. The first sync is on its way.' },
  denied: { ok: false, text: 'You did not allow access, so nothing was connected.' },
  expired: { ok: false, text: 'The link was too old or already used. Press Connect again.' },
  error: { ok: false, text: 'GitHub did not finish the connection. Try again in a minute.' },
};

/**
 * The person's GitHub account: Connect goes to GitHub's authorization page
 * and comes back to /account?github=…; the link shows the login and the last
 * sync, Sync now queues one (a minute apart at most), Disconnect revokes
 * after a confirm. A quiet line when the server has no OAuth App.
 */
export function GithubCard({
  go = (url: string) => window.location.assign(url),
  search = window.location.search,
  now = () => new Date(),
}: {
  go?: (url: string) => void;
  search?: string;
  now?: () => Date;
}) {
  const status = useGithubStatus();
  const unlink = useGithubUnlink();
  const sync = useGithubSync();
  const [sure, setSure] = useState(false);
  // Sync now sleeps until this instant after a 429 (sync_too_soon).
  const [retryAt, setRetryAt] = useState<number | null>(null);
  useEffect(() => {
    if (retryAt === null) return;
    const wait = retryAt - now().getTime();
    if (wait <= 0) {
      setRetryAt(null);
      return;
    }
    const t = setTimeout(() => setRetryAt(null), wait);
    return () => clearTimeout(t);
  }, [retryAt, now]);
  const outcome = OUTCOMES[new URLSearchParams(search).get('github') ?? ''];
  const s = status.data;
  const hint = orgHintLine(s?.org_hint ?? null);

  const syncNow = () =>
    sync.mutate(undefined, {
      onError: (e) => {
        if (e instanceof ApiClientError && e.status === 429) setRetryAt(syncRetryAt(e.body, now()));
      },
    });
  const tooSoon = retryAt !== null;

  return (
    <Card>
      <CardHeader>
        <CardTitle>GitHub</CardTitle>
        <CardDescription>
          Your repositories, yours and your organisations&apos;, kept fresh every six hours; the
          GitHub section says which need attention. The token stays sealed on the server.
        </CardDescription>
      </CardHeader>
      <CardContent className="space-y-3 text-sm">
        {outcome && (
          <Alert variant={outcome.ok ? 'default' : 'destructive'}>
            <AlertDescription>{outcome.text}</AlertDescription>
          </Alert>
        )}
        {!s ? (
          status.error instanceof ApiClientError && status.error.status === 404 ? (
            <p className="text-muted-foreground">The GitHub module is off on this server.</p>
          ) : null
        ) : !s.configured ? (
          <p className="text-muted-foreground">
            The server has no GitHub OAuth App yet (GITHUB_CLIENT_ID, GITHUB_CLIENT_SECRET and
            GITHUB_TOKEN_KEY in the Secret life-os-app).
          </p>
        ) : !s.linked ? (
          <Button variant="outline" onClick={() => go(OAUTH_START)}>
            Connect GitHub
          </Button>
        ) : (
          <>
            <p>
              Connected as {s.login || 'your GitHub account'}
              <span className="text-muted-foreground" data-num-ok>
                {' · '}
                {sync.isSuccess && !sync.isPending ? 'sync queued' : syncedAgo(s.synced_at, now())}
              </span>
            </p>
            {s.sync_error && (
              <Alert variant="destructive">
                <AlertDescription>
                  The last sync failed: {s.sync_error}. The list shown is the one before it.
                </AlertDescription>
              </Alert>
            )}
            {hint && <p className="text-muted-foreground">{hint}</p>}
            <div className="flex flex-wrap items-center gap-2">
              <Button
                variant="outline"
                size="sm"
                disabled={sync.isPending || tooSoon}
                onClick={syncNow}
              >
                {tooSoon ? 'Synced a moment ago' : 'Sync now'}
              </Button>
              {sure ? (
                <>
                  <span>Life OS forgets the token and the list; GitHub revokes it.</span>
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
                </>
              ) : (
                <Button variant="outline" size="sm" onClick={() => setSure(true)}>
                  Disconnect…
                </Button>
              )}
            </div>
            <p className="text-muted-foreground">
              <Link to="/github" className="underline">
                Open the GitHub section
              </Link>
            </p>
          </>
        )}
        {(unlink.error || (sync.error && !tooSoon)) && (
          <Alert variant="destructive">
            <AlertDescription>{unlink.error || sync.error}</AlertDescription>
          </Alert>
        )}
      </CardContent>
    </Card>
  );
}
