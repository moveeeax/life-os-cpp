import { useEffect, useState } from 'react';

import { isFinalStatus, useCoverage, useProbe, useStartSync, useSyncRun } from '@/hooks/useHealth';
import { apiErrorMessage } from '@/lib/api/client';
import { rangeForDays, today } from '@/lib/health';
import { SYNC_DATA_TYPES } from '@/lib/health/types';

import { Num, Row } from '@/shell/kit';

const inputClass = 'k-input';
const primaryButton = 'k-btn k-solid';
const secondaryButton = 'k-btn';

const label = (type: string) => type.replace(/_/g, ' ');

function CoverageTable() {
  const q = useCoverage();
  if (q.isPending) {
    return <div className="h-40 animate-pulse rounded-xl bg-gray-100 dark:bg-white/5" />;
  }
  if (q.error) {
    return (
      <p role="alert" className="text-theme-sm text-error-500">
        {apiErrorMessage(q.error, 'Failed to load coverage.')}{' '}
        <button type="button" className="underline" onClick={() => q.refetch()}>
          Retry
        </button>
      </p>
    );
  }
  const rows = Object.entries(q.data ?? {}).sort(([a], [b]) => (a < b ? -1 : 1));
  return (
    <div className="k-group-rows">
      {rows.map(([type, c]) => (
        <div key={type} className="k-kv k-cov">
          <span>{label(type)}</span>
          <span className="k-muted">
            {c.first_date ?? '–'} – {c.last_date ?? '–'} ·{' '}
            <Num>{c.records.toLocaleString('en-US')}</Num> records
            {c.last_sync_at && <> · synced {new Date(c.last_sync_at).toLocaleString()}</>}
          </span>
        </div>
      ))}
    </div>
  );
}

/** Sync controls: coverage, a form that starts a sync run, and a cloud probe. */
export function SyncPanel() {
  const initial = rangeForDays(2, today());
  const [from, setFrom] = useState(initial.from);
  const [to, setTo] = useState(initial.to);
  const [types, setTypes] = useState<string[]>([...SYNC_DATA_TYPES]);
  const [runId, setRunId] = useState<number | null>(null);

  const start = useStartSync();
  const run = useSyncRun(runId);
  const probe = useProbe();

  // Watching a run: started, not final yet, and the status request has not failed.
  const watching = runId !== null && !run.error && !(run.data && isFinalStatus(run.data.status));
  const running = start.isPending || watching;

  // A run that stays queued or running this long is most likely stuck (worker
  // down). Say so instead of spinning for ever.
  const [slow, setSlow] = useState(false);
  useEffect(() => {
    setSlow(false);
    if (!watching) return;
    const t = setTimeout(() => setSlow(true), 10 * 60 * 1000);
    return () => clearTimeout(t);
  }, [watching, runId]);

  const stopWatching = () => {
    setRunId(null);
    start.reset();
  };
  const rangeValid = from !== '' && to !== '' && from <= to;
  const canStart = rangeValid && types.length > 0 && !running;

  const toggleType = (type: string) =>
    setTypes((prev) => (prev.includes(type) ? prev.filter((t) => t !== type) : [...prev, type]));

  const onSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!canStart) return;
    try {
      const res = await start.mutateAsync({ from, to, data_types: types });
      setRunId(res.run_id);
    } catch {
      // The error is rendered from start.error below.
    }
  };

  return (
    <Row title="Sync" sub="What is stored per data type, and a manual sync from the Xiaomi cloud">
      <div className="mt-4">
        <CoverageTable />
      </div>

      <form onSubmit={onSubmit} className="mt-6 flex flex-col gap-4">
        <div className="flex flex-wrap items-end gap-4">
          <div>
            <label htmlFor="sync-from" className="k-label">
              From
            </label>
            <input
              id="sync-from"
              type="date"
              value={from}
              max={to || undefined}
              onChange={(e) => setFrom(e.target.value)}
              className={inputClass}
            />
          </div>
          <div>
            <label htmlFor="sync-to" className="k-label">
              To
            </label>
            <input
              id="sync-to"
              type="date"
              value={to}
              min={from || undefined}
              onChange={(e) => setTo(e.target.value)}
              className={inputClass}
            />
          </div>
        </div>

        <fieldset>
          <legend className="k-label">Data types</legend>
          <div className="flex flex-wrap gap-x-5 gap-y-2">
            {SYNC_DATA_TYPES.map((type) => (
              <label key={type} className="k-check">
                <input
                  type="checkbox"
                  checked={types.includes(type)}
                  onChange={() => toggleType(type)}
                  className="size-4 accent-brand-500"
                />
                {label(type)}
              </label>
            ))}
          </div>
        </fieldset>

        {!rangeValid && (
          <p role="alert" className="text-theme-sm text-error-500">
            Pick both dates; From must not be after To.
          </p>
        )}
        {types.length === 0 && (
          <p role="alert" className="text-theme-sm text-error-500">
            Pick at least one data type.
          </p>
        )}

        <div className="flex flex-wrap items-center gap-3">
          <button type="submit" disabled={!canStart} className={primaryButton}>
            {running ? 'Sync in progress…' : 'Start sync'}
          </button>
          {runId !== null && (
            <button type="button" onClick={stopWatching} className={secondaryButton}>
              {watching ? 'Stop watching' : 'Clear'}
            </button>
          )}
          <button
            type="button"
            disabled={!rangeValid || probe.isPending || running}
            onClick={() => probe.mutate({ from, to })}
            className={secondaryButton}
          >
            {probe.isPending ? 'Checking…' : 'Check cloud connection'}
          </button>
        </div>
      </form>

      <div className="mt-4 flex flex-col gap-2 text-theme-sm" aria-live="polite">
        {start.error != null && (
          <p role="alert" className="text-error-500">
            {apiErrorMessage(start.error, 'Could not start the sync.')}
          </p>
        )}
        {run.error != null && (
          <p role="alert" className="text-error-500">
            Could not read the status of run #{runId}:{' '}
            {apiErrorMessage(run.error, 'request failed')}. The run itself may still be going.{' '}
            <button type="button" className="underline" onClick={() => run.refetch()}>
              Check again
            </button>
          </p>
        )}
        {slow && watching && (
          <p role="alert" className="text-warning-600 dark:text-warning-500">
            This run has not finished in 10 minutes. The worker may be down; you can stop watching
            and check the coverage table later.
          </p>
        )}
        {run.data && (
          <div className="k-banner k-col">
            <p className="text-gray-800 dark:text-white/90">
              Run #{run.data.id}: <span className="font-medium">{run.data.status}</span>
              {run.data.finished_at === null && ' …'}
            </p>
            {isFinalStatus(run.data.status) && run.data.result && (
              <pre className="mt-2 overflow-x-auto text-theme-xs text-gray-500 dark:text-gray-400">
                {JSON.stringify(run.data.result, null, 2)}
              </pre>
            )}
          </div>
        )}
        {probe.error != null && (
          <p role="alert" className="text-error-500">
            {apiErrorMessage(probe.error, 'Cloud check failed.')}
          </p>
        )}
        {probe.data && (
          <p className="text-success-600 dark:text-success-500">
            Cloud reachable: account {probe.data.account}, region {probe.data.region},{' '}
            {probe.data.records} step records in the selected dates.
          </p>
        )}
      </div>
    </Row>
  );
}
