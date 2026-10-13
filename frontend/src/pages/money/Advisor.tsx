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
  textareaClass,
} from '@/shell/kit/styles';
import { Group, Row, Sheet } from '@/shell/kit';

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
  const [settings, setSettings] = useState(false);
  const run = useRunAdvisor();
  const list = reports.data ?? [];
  const current = list.find((r) => r.id === selected) ?? list[0];
  const report = useAdvisorReport(current?.id, current?.status);

  return (
    <MoneyFrame
      title="Advisor"
      actions={
        <>
          <select
            value={period}
            onChange={(e) => setPeriod(e.target.value as typeof period)}
            aria-label="Period to review"
            className="k-input k-compact"
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
          <span className="k-grow" />
          <button type="button" className="k-btn" onClick={() => setSettings(true)}>
            Settings
          </button>
        </>
      }
    >
      {run.error && (
        <p role="alert" className="k-error">
          {String(run.error)}
        </p>
      )}
      {reports.isPending ? (
        <Placeholder />
      ) : reports.isError ? (
        <LoadError error={reports.error} onRetry={() => void reports.refetch()} />
      ) : !current ? (
        <p className="k-empty">
          No reviews yet. Run one now or switch on the weekly review in Settings.
        </p>
      ) : (
        <>
          <p className="k-sub">
            {current.period} {current.period_start} – {current.period_end}
            {current.model && ` · ${current.model}`}
          </p>
          <div className="k-review">
            {current.status === 'queued' || current.status === 'running' ? (
              <p className="k-sub">The advisor is writing…</p>
            ) : current.status === 'failed' ? (
              <p role="alert" className="k-error">
                {parseErrorText(current.error)}
              </p>
            ) : report.data?.content ? (
              <Review content={report.data.content} />
            ) : (
              <Placeholder />
            )}
          </div>
          {report.data?.facts && (
            <details className="k-details">
              <summary>The numbers this review was built on</summary>
              <pre className="num">{JSON.stringify(report.data.facts, null, 2)}</pre>
            </details>
          )}
          <Group label="Reviews">
            {list.map((r) => (
              <Row
                key={r.id}
                title={
                  <button type="button" className="k-link-plain" onClick={() => setSelected(r.id)}>
                    {r.period} {r.period_start}
                    {r.id === current.id ? ' · shown' : ''}
                  </button>
                }
                right={
                  r.status === 'done' ? undefined : <span className="k-muted">{r.status}</span>
                }
              />
            ))}
          </Group>
        </>
      )}
      {settings && (
        <Sheet title="Advisor settings" onClose={() => setSettings(false)} width={560}>
          <SettingsForm />
        </Sheet>
      )}
    </MoneyFrame>
  );
}
