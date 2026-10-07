import { useEffect, useState, type FormEvent } from 'react';

import {
  useAdvisorReport,
  useAdvisorReports,
  useCurrencies,
  useMoneySettings,
  useRunAdvisor,
  useSaveSettings,
} from '@/hooks/useMoney';
import { parseErrorText, todayLocal } from '@/lib/ledger';
import { parseMarkdown, type Inline } from '@/lib/ledger/markdown';
import { cn } from '@/lib/utils';

import {
  cardClass,
  inputClass,
  labelClass,
  primaryButton,
  secondaryButton,
  textareaClass,
} from '../workout/styles';
import { LoadError, MoneyFrame, Placeholder } from './frame';

const WEEKDAYS = ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday'];

function Parts({ parts }: { parts: Inline[] }) {
  return (
    <>
      {parts.map((p, i) =>
        p.bold ? (
          <strong key={i} className="font-semibold text-gray-800 dark:text-white/90">
            {p.text}
          </strong>
        ) : (
          <span key={i}>{p.text}</span>
        ),
      )}
    </>
  );
}

/** The review as text; nothing of it is read as HTML. */
function Review({ content }: { content: string }) {
  return (
    <div className="flex flex-col gap-3 text-theme-sm leading-relaxed text-gray-700 dark:text-gray-300">
      {parseMarkdown(content).map((b, i) =>
        b.kind === 'heading' ? (
          <h3
            key={i}
            className={cn(
              'font-semibold text-gray-800 dark:text-white/90',
              b.level === 2 ? 'text-base' : 'text-theme-sm',
            )}
          >
            <Parts parts={b.parts} />
          </h3>
        ) : b.kind === 'list' ? (
          <ul key={i} className="list-disc ps-5">
            {b.items.map((item, j) => (
              <li key={j}>
                <Parts parts={item} />
              </li>
            ))}
          </ul>
        ) : (
          <p key={i}>
            <Parts parts={b.parts} />
          </p>
        ),
      )}
    </div>
  );
}

function SettingsForm() {
  const settings = useMoneySettings().data;
  const currencies = useCurrencies().data ?? [];
  const save = useSaveSettings(() => setSaved(true));
  const [enabled, setEnabled] = useState(false);
  const [weekday, setWeekday] = useState(1);
  const [note, setNote] = useState('');
  const [saved, setSaved] = useState(false);
  useEffect(() => {
    if (!settings) return;
    setEnabled(settings.advisor_enabled);
    setWeekday(settings.advisor_weekday);
    setNote(settings.advisor_note);
  }, [settings]);
  if (!settings) return null;

  const submit = (e: FormEvent) => {
    e.preventDefault();
    save.mutate({
      view_currency: settings.view_currency ?? null,
      advisor_enabled: enabled,
      advisor_weekday: weekday,
      advisor_currencies: settings.advisor_currencies.filter((c) =>
        currencies.some((x) => x.code === c),
      ),
      advisor_note: note,
    });
  };

  return (
    <form onSubmit={submit} className={cardClass} aria-labelledby="advisor-settings">
      <h2
        id="advisor-settings"
        className="text-base font-semibold text-gray-800 dark:text-white/90"
      >
        Weekly review
      </h2>
      <label className="mt-3 flex items-center gap-2 text-theme-sm">
        <input
          type="checkbox"
          checked={enabled}
          onChange={(e) => {
            setSaved(false);
            setEnabled(e.target.checked);
          }}
        />
        Write a review of the past week every
      </label>
      <div className="mt-3 grid gap-3 sm:grid-cols-2">
        <div>
          <label htmlFor="advisor-weekday" className={labelClass}>
            Day
          </label>
          <select
            id="advisor-weekday"
            value={weekday}
            onChange={(e) => {
              setSaved(false);
              setWeekday(Number(e.target.value));
            }}
            className={inputClass}
          >
            {WEEKDAYS.map((d, i) => (
              <option key={d} value={i + 1}>
                {d}
              </option>
            ))}
          </select>
        </div>
        <div className="sm:col-span-2">
          <label htmlFor="advisor-note" className={labelClass}>
            What the advisor should keep in mind (its language follows this note)
          </label>
          <textarea
            id="advisor-note"
            value={note}
            onChange={(e) => {
              setSaved(false);
              setNote(e.target.value);
            }}
            rows={3}
            maxLength={2000}
            className={textareaClass}
          />
        </div>
      </div>
      {save.error && (
        <p role="alert" className="mt-3 text-theme-sm text-error-500">
          {save.error}
        </p>
      )}
      <div className="mt-4 flex items-center justify-end gap-3">
        {saved && <span className="text-theme-sm text-success-600">Saved.</span>}
        <button type="submit" disabled={save.isPending} className={primaryButton}>
          {save.isPending ? 'Saving…' : 'Save'}
        </button>
      </div>
    </form>
  );
}

/** The advisor's reviews, a run on demand, and the weekly schedule. */
export function MoneyAdvisorPage() {
  const reports = useAdvisorReports();
  const [selected, setSelected] = useState<string | undefined>(undefined);
  const [period, setPeriod] = useState<'week' | 'month' | 'quarter'>('week');
  const run = useRunAdvisor();
  const list = reports.data ?? [];
  const current = list.find((r) => r.id === selected) ?? list[0];
  const report = useAdvisorReport(current?.id, current?.status);

  return (
    <MoneyFrame
      title="Advisor"
      actions={
        <div className="flex flex-wrap items-center gap-2">
          <select
            value={period}
            onChange={(e) => setPeriod(e.target.value as typeof period)}
            aria-label="Period to review"
            className={cn(inputClass, 'h-10 w-auto')}
          >
            <option value="week">This week</option>
            <option value="month">This month</option>
            <option value="quarter">This quarter</option>
          </select>
          <button
            type="button"
            onClick={() => run.mutate({ period, date: todayLocal() })}
            disabled={run.isPending}
            className={primaryButton}
          >
            {run.isPending ? 'Asking…' : 'Run now'}
          </button>
        </div>
      }
    >
      {run.error && (
        <p role="alert" className="text-theme-sm text-error-500">
          {run.error}
        </p>
      )}
      {reports.isPending ? (
        <Placeholder className="h-48" />
      ) : reports.isError ? (
        <LoadError error={reports.error} onRetry={() => void reports.refetch()} />
      ) : !current ? (
        <p className={cn(cardClass, 'text-center text-theme-sm text-gray-500')}>
          No reviews yet. Run one now or switch on the weekly review below.
        </p>
      ) : (
        <div className="grid gap-4 lg:grid-cols-[1fr_16rem]">
          <section className={cardClass} aria-label="Review">
            <p className="text-theme-xs text-gray-500">
              {current.period} {current.period_start} – {current.period_end}
              {current.model && ` · ${current.model}`}
            </p>
            <div className="mt-3">
              {current.status === 'queued' || current.status === 'running' ? (
                <p className="text-theme-sm text-gray-500">The advisor is writing…</p>
              ) : current.status === 'failed' ? (
                <p role="alert" className="text-theme-sm text-error-500">
                  {parseErrorText(current.error)}
                </p>
              ) : report.data?.content ? (
                <Review content={report.data.content} />
              ) : (
                <Placeholder className="h-24" />
              )}
            </div>
            {report.data?.facts && (
              <details className="mt-4 text-theme-xs text-gray-500">
                <summary className="cursor-pointer">The numbers this review was built on</summary>
                <pre className="mt-2 max-h-80 overflow-auto rounded-lg bg-gray-50 p-3 dark:bg-white/5">
                  {JSON.stringify(report.data.facts, null, 2)}
                </pre>
              </details>
            )}
          </section>
          <section className={cardClass} aria-label="Earlier reviews">
            <h2 className="text-theme-sm font-semibold text-gray-800 dark:text-white/90">
              Reviews
            </h2>
            <ul className="mt-2 flex flex-col gap-1">
              {list.map((r) => (
                <li key={r.id}>
                  <button
                    type="button"
                    onClick={() => setSelected(r.id)}
                    className={cn(
                      secondaryButton,
                      'w-full justify-between px-3 py-2 text-theme-xs',
                      r.id === current.id && 'border-brand-500 text-brand-500',
                    )}
                  >
                    <span>
                      {r.period} {r.period_start}
                    </span>
                    <span>{r.status === 'done' ? '' : r.status}</span>
                  </button>
                </li>
              ))}
            </ul>
          </section>
        </div>
      )}
      <SettingsForm />
    </MoneyFrame>
  );
}
