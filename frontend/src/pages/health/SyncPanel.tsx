import { useState } from 'react';

import { isFinalStatus, useCoverage, useProbe, useStartSync, useSyncRun } from '@/hooks/useHealth';
import { apiErrorMessage } from '@/lib/api/client';
import { rangeForDays, today } from '@/lib/health';
import { SYNC_DATA_TYPES } from '@/lib/health/types';

import { cardClass } from './ChartCard';

const inputClass =
  'h-11 rounded-lg border border-gray-300 bg-transparent px-3 text-sm text-gray-800 shadow-theme-xs focus:border-brand-300 focus:ring-3 focus:ring-brand-500/20 focus:outline-hidden dark:border-gray-700 dark:bg-gray-900 dark:text-white/90';
const primaryButton =
  'inline-flex items-center justify-center rounded-lg bg-brand-500 px-4 py-3 text-sm font-medium text-white shadow-theme-xs transition hover:bg-brand-600 disabled:cursor-not-allowed disabled:bg-brand-300';
const secondaryButton =
  'inline-flex items-center justify-center rounded-lg border border-gray-300 px-4 py-3 text-sm font-medium text-gray-700 transition hover:bg-gray-100 disabled:cursor-not-allowed disabled:opacity-50 dark:border-gray-700 dark:text-gray-300 dark:hover:bg-white/5';

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
    <div className="overflow-x-auto">
      <table className="w-full text-left text-theme-sm">
        <thead>
          <tr className="border-b border-gray-200 text-theme-xs text-gray-500 uppercase dark:border-gray-800 dark:text-gray-400">
            <th scope="col" className="py-2 pe-4 font-medium">
              Type
            </th>
            <th scope="col" className="py-2 pe-4 font-medium">
              First date
            </th>
            <th scope="col" className="py-2 pe-4 font-medium">
              Last date
            </th>
            <th scope="col" className="py-2 pe-4 text-right font-medium">
              Records
            </th>
            <th scope="col" className="py-2 font-medium">
              Last sync
            </th>
          </tr>
        </thead>
        <tbody>
          {rows.map(([type, c]) => (
            <tr key={type} className="border-b border-gray-100 last:border-0 dark:border-gray-800">
              <td className="py-2 pe-4 text-gray-800 dark:text-white/90">{label(type)}</td>
              <td className="py-2 pe-4 text-gray-500 dark:text-gray-400">{c.first_date ?? '–'}</td>
              <td className="py-2 pe-4 text-gray-500 dark:text-gray-400">{c.last_date ?? '–'}</td>
              <td className="py-2 pe-4 text-right text-gray-800 tabular-nums dark:text-white/90">
                {c.records.toLocaleString('en-US')}
              </td>
              <td className="py-2 text-gray-500 dark:text-gray-400">
                {c.last_sync_at ? new Date(c.last_sync_at).toLocaleString() : '–'}
              </td>
            </tr>
          ))}
        </tbody>
      </table>
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

  const running = start.isPending || (run.data ? !isFinalStatus(run.data.status) : runId !== null);
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
    <section className={cardClass} aria-label="Sync">
      <h2 className="text-lg font-semibold text-gray-800 dark:text-white/90">Sync</h2>
      <p className="mt-1 text-theme-sm text-gray-500 dark:text-gray-400">
        What is stored per data type, and a manual sync from the Xiaomi cloud.
      </p>

      <div className="mt-4">
        <CoverageTable />
      </div>

      <form onSubmit={onSubmit} className="mt-6 flex flex-col gap-4">
        <div className="flex flex-wrap items-end gap-4">
          <div>
            <label
              htmlFor="sync-from"
              className="mb-1.5 block text-sm font-medium text-gray-700 dark:text-gray-400"
            >
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
            <label
              htmlFor="sync-to"
              className="mb-1.5 block text-sm font-medium text-gray-700 dark:text-gray-400"
            >
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
          <legend className="mb-2 text-sm font-medium text-gray-700 dark:text-gray-400">
            Data types
          </legend>
          <div className="flex flex-wrap gap-x-5 gap-y-2">
            {SYNC_DATA_TYPES.map((type) => (
              <label
                key={type}
                className="flex items-center gap-2 text-theme-sm text-gray-700 dark:text-gray-300"
              >
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
            {apiErrorMessage(run.error, 'Could not read the sync status.')}
          </p>
        )}
        {run.data && (
          <div className="rounded-xl border border-gray-200 p-4 dark:border-gray-800">
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
    </section>
  );
}
