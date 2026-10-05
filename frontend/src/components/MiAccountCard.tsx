import { useEffect, useState } from 'react';

import { Alert, AlertDescription } from '@/components/ui/alert';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Skeleton } from '@/components/ui/skeleton';
import { useProbe } from '@/hooks/useHealth';
import {
  useDetectMiRegion,
  useMiAccount,
  useMiLink,
  useSetMiRegion,
  useUnlinkMi,
} from '@/hooks/useMiAccount';
import { ApiClientError, apiErrorMessage } from '@/lib/api/client';
import { today } from '@/lib/health';
import {
  REGIONS,
  formatCountdown,
  linkErrorText,
  miView,
  reauthReason,
  regionLabel,
  secondsLeft,
  type MiLinkStart,
  type MiStatus,
} from '@/lib/mi';

const when = (iso: string | null | undefined): string =>
  iso ? new Date(iso).toLocaleString() : 'never';

function Fact({ label, value }: { label: string; value: string }) {
  return (
    <div>
      <span className="text-muted-foreground">{label}: </span>
      {value}
    </div>
  );
}

/** The QR, the link for a phone and the time left. */
function Waiting({ attempt, onCancel }: { attempt: MiLinkStart; onCancel: () => void }) {
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    const tick = setInterval(() => setNow(Date.now()), 1000);
    return () => clearInterval(tick);
  }, []);

  return (
    <div className="space-y-4">
      <div className="flex flex-col items-center gap-4 sm:flex-row sm:items-start">
        {/* White in both themes: a QR needs dark modules on a light ground. */}
        <img
          src={`data:image/png;base64,${attempt.qr_png_base64}`}
          alt="QR code for the Xiaomi sign-in"
          width={240}
          height={240}
          className="size-60 shrink-0 rounded-lg border bg-white p-2"
        />
        <ol className="list-decimal space-y-2 ps-5 text-sm">
          <li>Scan the code with a Xiaomi app (Mi Fitness or Mi Home) and confirm the sign-in.</li>
          <li>
            On this phone? Open{' '}
            <a
              href={attempt.confirm_url}
              target="_blank"
              rel="noopener noreferrer"
              className="font-medium text-primary underline"
            >
              Confirm in Xiaomi
            </a>{' '}
            instead and confirm there.
          </li>
          <li>Come back here: the page notices by itself.</li>
        </ol>
      </div>
      <div className="flex flex-wrap items-center gap-3">
        <p className="text-sm text-muted-foreground" role="status">
          Waiting for the confirmation. The code is valid for{' '}
          <span className="font-medium tabular-nums">
            {formatCountdown(secondsLeft(attempt.expires_at, now))}
          </span>
          .
        </p>
        <Button variant="outline" onClick={onCancel}>
          Cancel
        </Button>
      </div>
    </div>
  );
}

function Linked({ status }: { status: MiStatus }) {
  const [confirmingUnlink, setConfirmingUnlink] = useState(false);
  const [deleteData, setDeleteData] = useState(false);
  const unlink = useUnlinkMi(() => setConfirmingUnlink(false));
  const setRegion = useSetMiRegion();
  const detect = useDetectMiRegion();
  const probe = useProbe();
  const day = today();
  const busy = unlink.isPending || setRegion.isPending || detect.isPending;

  return (
    <div className="space-y-4">
      <div className="space-y-1 text-sm">
        <Fact label="Account" value={status.account ?? '–'} />
        <Fact label="Region" value={regionLabel(status.region)} />
        <Fact label="Linked" value={when(status.linked_at)} />
        <Fact label="Last sign-in Xiaomi accepted" value={when(status.last_ok_at)} />
        <Fact
          label="Last sync"
          value={
            status.last_sync
              ? `${status.last_sync.status}, ${when(status.last_sync.finished_at)}`
              : 'none yet'
          }
        />
      </div>

      {status.region_detected === false && (
        <Alert>
          <AlertDescription className="space-y-3">
            <p>
              No data was found for this account in any region yet, so the region is a guess. Pick
              the region of your Mi Fitness account, or detect again after the band has synced with
              the phone.
            </p>
            <div className="flex flex-wrap items-center gap-3">
              <label htmlFor="mi-region" className="text-sm font-medium">
                Region
              </label>
              <select
                id="mi-region"
                value={status.region ?? 'cn'}
                disabled={busy}
                onChange={(e) => setRegion.mutate(e.target.value)}
                className="h-9 rounded-md border border-input bg-background px-2 text-sm"
              >
                {REGIONS.map((r) => (
                  <option key={r.value} value={r.value}>
                    {r.label}
                  </option>
                ))}
              </select>
              <Button variant="outline" disabled={busy} onClick={() => detect.mutate()}>
                {detect.isPending ? 'Detecting…' : 'Detect again'}
              </Button>
            </div>
          </AlertDescription>
        </Alert>
      )}

      {confirmingUnlink ? (
        <div
          className="space-y-3 rounded-md border p-4"
          role="group"
          aria-label="Unlink Mi account"
        >
          <p className="text-sm">
            Unlink this Mi account? Syncing stops. The data already synced stays unless you delete
            it here.
          </p>
          <label className="flex items-center gap-2 text-sm">
            <input
              type="checkbox"
              checked={deleteData}
              onChange={(e) => setDeleteData(e.target.checked)}
              className="size-4"
            />
            Also delete the data synced from this account
          </label>
          <div className="flex flex-wrap gap-3">
            <Button
              variant={deleteData ? 'destructive' : 'default'}
              disabled={unlink.isPending}
              onClick={() => unlink.mutate(deleteData)}
            >
              {unlink.isPending ? 'Unlinking…' : deleteData ? 'Unlink and delete data' : 'Unlink'}
            </Button>
            <Button
              variant="outline"
              disabled={unlink.isPending}
              onClick={() => {
                setConfirmingUnlink(false);
                setDeleteData(false);
                unlink.clearError();
              }}
            >
              Keep linked
            </Button>
          </div>
        </div>
      ) : (
        <div className="flex flex-wrap gap-3">
          <Button
            variant="outline"
            disabled={probe.isPending || busy}
            onClick={() => probe.mutate({ from: day, to: day })}
          >
            {probe.isPending ? 'Checking…' : 'Check now'}
          </Button>
          <Button variant="outline" disabled={busy} onClick={() => setConfirmingUnlink(true)}>
            Unlink
          </Button>
        </div>
      )}

      {probe.data && !probe.isPending && (
        <p className="text-sm text-muted-foreground" role="status">
          Xiaomi answered: {probe.data.records} step{' '}
          {probe.data.records === 1 ? 'record' : 'records'} for today.
        </p>
      )}
      {(probe.error || unlink.error || setRegion.error || detect.error) && (
        <Alert variant="destructive">
          <AlertDescription>
            {unlink.error ??
              setRegion.error ??
              detect.error ??
              apiErrorMessage(probe.error, 'The check failed.')}
          </AlertDescription>
        </Alert>
      )}
    </div>
  );
}

/**
 * Profile block for the user's Mi Fitness account: link it by a QR sign-in,
 * see whether the saved sign-in still works, unlink it. Not rendered while
 * the fitness module is off (the status route answers 404).
 */
export function MiAccountCard() {
  const account = useMiAccount();
  const link = useMiLink();

  if (account.error instanceof ApiClientError && account.error.status === 404) return null;

  const status = account.data;
  const view = miView(status);

  const startButton = (label: string) => (
    <Button onClick={() => void link.start()} disabled={link.state === 'starting'}>
      {link.state === 'starting' ? 'Asking Xiaomi…' : label}
    </Button>
  );

  return (
    <Card>
      <CardHeader>
        <CardTitle>Mi Fitness</CardTitle>
        <CardDescription>
          {view === 'linked'
            ? 'Linked. Your band data syncs to Health and Workout.'
            : view === 'relink'
              ? 'Linked, but the sign-in has to be renewed.'
              : 'Link your Xiaomi account to sync steps, sleep, heart rate and weight.'}
        </CardDescription>
      </CardHeader>
      <CardContent className="space-y-4">
        {account.isPending ? (
          <Skeleton className="h-24 w-full" />
        ) : account.error ? (
          <Alert variant="destructive">
            <AlertDescription className="flex flex-wrap items-center gap-3">
              {apiErrorMessage(account.error, 'Failed to load the link status.')}
              <Button variant="outline" onClick={() => void account.refetch()}>
                Retry
              </Button>
            </AlertDescription>
          </Alert>
        ) : link.state === 'waiting' && link.attempt ? (
          <Waiting attempt={link.attempt} onCancel={link.cancel} />
        ) : (
          <>
            {link.state === 'expired' && (
              <Alert>
                <AlertDescription>The code expired before it was confirmed.</AlertDescription>
              </Alert>
            )}
            {link.state === 'failed' && (
              <Alert variant="destructive">
                <AlertDescription>{linkErrorText(link.error)}</AlertDescription>
              </Alert>
            )}

            {view === 'linked' && status && <Linked status={status} />}

            {view === 'relink' && status && (
              <>
                <Alert variant="destructive">
                  <AlertDescription>
                    {reauthReason(status.last_error)} Syncing is paused until you link the account
                    again. The data already synced stays.
                  </AlertDescription>
                </Alert>
                <div className="space-y-1 text-sm">
                  <Fact label="Account" value={status.account ?? '–'} />
                  <Fact label="Last sign-in Xiaomi accepted" value={when(status.last_ok_at)} />
                </div>
                {startButton(link.state === 'idle' ? 'Link again' : 'Try again')}
              </>
            )}

            {view === 'none' && (
              <>
                <p className="text-sm text-muted-foreground">
                  You confirm the sign-in in a Xiaomi app; your Xiaomi password is never entered
                  here.
                </p>
                {startButton(
                  link.state === 'expired' || link.state === 'failed'
                    ? 'Try again'
                    : 'Link Mi account',
                )}
              </>
            )}
          </>
        )}
      </CardContent>
    </Card>
  );
}
