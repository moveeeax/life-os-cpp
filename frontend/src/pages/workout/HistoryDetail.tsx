import { useEffect, useState, type FormEvent } from 'react';
import { useQueryClient } from '@tanstack/react-query';
import { Link, useNavigate, useParams } from 'react-router';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { isFinalStatus, useStartSync, useSyncRun } from '@/hooks/useHealth';
import { LinkMiNotice } from '@/components/LinkMiNotice';
import { useMe } from '@/hooks/useMe';
import { useMiAccount } from '@/hooks/useMiAccount';
import {
  useDeleteSession,
  usePatchSession,
  useSession,
  useSessionHeartRate,
  useSetQueue,
} from '@/hooks/useWorkoutSession';
import { apiErrorMessage } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import { Permission, userCan } from '@/lib/auth/permissions';
import { rangeForDays, today } from '@/lib/health';
import { SYNC_DATA_TYPES } from '@/lib/health/types';
import { needsLink } from '@/lib/mi';
import { formatDay, formatDuration, formatTime, formatVolume } from '@/lib/workout/format';
import {
  describeSet,
  durationMinutes,
  fromLocalInput,
  mergeSets,
  sessionVolume,
  toLocalInput,
  workSetCount,
} from '@/lib/workout/session';
import type { WorkoutSession, WorkoutSessionPatch } from '@/lib/workout/types';
import { cn } from '@/lib/utils';

import { HealthBadge, Stat } from './bits';
import { ExerciseLog } from './ExerciseLog';
import { HeartRateChart } from './HeartRateChart';
import { LoadError, WorkoutFrame } from './frame';
import {
  cardClass,
  dangerButton,
  inputClass,
  labelClass,
  primaryButton,
  secondaryButton,
  textareaClass,
} from './styles';

const heading = 'text-lg font-semibold text-gray-800 dark:text-white/90';
const muted = 'text-theme-sm text-gray-500 dark:text-gray-400';

/** What the band recorded during the workout, or why there is nothing yet. */
function HealthBlock({ session }: { session: WorkoutSession & { finished_at: string } }) {
  const qc = useQueryClient();
  const user = useMe().data ?? null;
  const canSync = userCan(user, Permission.FitnessSync);
  const unlinked = needsLink(useMiAccount(canSync).data);
  const hr = useSessionHeartRate(session.id, true);
  const startSync = useStartSync();
  const [runId, setRunId] = useState<number | null>(null);
  const run = useSyncRun(runId);
  const syncDone = run.data ? isFinalStatus(run.data.status) : false;

  // The server matches sessions right after it stores the synced data, a
  // moment after the run is marked final: ask now and once more shortly after.
  useEffect(() => {
    if (!syncDone) return;
    const refresh = () => {
      void qc.invalidateQueries({ queryKey: qk.workout.session(session.id) });
      void qc.invalidateQueries({ queryKey: qk.workout.heartRate(session.id) });
      void qc.invalidateQueries({ queryKey: qk.workout.sessions() });
    };
    refresh();
    const again = setTimeout(refresh, 5000);
    return () => clearTimeout(again);
  }, [syncDone, qc, session.id]);

  const samples = hr.data?.samples ?? [];
  const latest = hr.data?.latest_sample_at ?? null;
  const syncing = startSync.isPending || (runId !== null && !syncDone && !run.error);

  return (
    <section className={cardClass} aria-label="Health">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <h2 className={heading}>Health</h2>
        <HealthBadge status={session.health_status} />
      </div>

      {session.health_status === 'matched' && (
        <div className="mt-4 grid grid-cols-2 gap-4 sm:grid-cols-4">
          <Stat
            label="Mean heart rate"
            value={session.hr_avg == null ? '–' : `${session.hr_avg} bpm`}
          />
          <Stat
            label="Max heart rate"
            value={session.hr_max == null ? '–' : `${session.hr_max} bpm`}
          />
          <Stat label="Band samples" value={String(session.hr_samples)} />
          <Stat
            label="Band workout"
            value={
              session.band_workout_id == null
                ? 'none'
                : session.band_calories_kcal == null
                  ? 'recorded'
                  : `${Math.round(session.band_calories_kcal)} kcal`
            }
          />
        </div>
      )}

      {session.health_status === 'pending' && unlinked && (
        // No band data can arrive without an account to take it from.
        <div className="mt-3">
          <LinkMiNotice purpose="to attach heart rate from your band to workouts" compact />
        </div>
      )}
      {session.health_status === 'pending' && !unlinked && (
        <p className={cn(muted, 'mt-2')}>
          The band has not synced past the end of this workout yet.{' '}
          {latest
            ? `Its newest heart-rate sample is from ${formatDay(latest)}, ${formatTime(latest)}.`
            : hr.isPending
              ? ''
              : 'There are no heart-rate samples at all.'}{' '}
          The data attaches by itself after the next sync.
        </p>
      )}
      {session.health_status === 'no_data' && (
        <p className={cn(muted, 'mt-2')}>
          The band synced past this workout and recorded nothing inside it.
        </p>
      )}

      {samples.length >= 2 ? (
        <div className="mt-2">
          <HeartRateChart
            samples={samples}
            startedAt={session.started_at}
            finishedAt={session.finished_at}
          />
        </div>
      ) : (
        samples.length === 1 && (
          <p className={cn(muted, 'mt-2')}>
            One sample inside the workout: {samples[0].bpm} bpm at{' '}
            {formatTime(samples[0].timestamp)}.
          </p>
        )
      )}
      {hr.error && (
        <p role="alert" className="mt-2 text-theme-sm text-error-500">
          {apiErrorMessage(hr.error, 'Failed to load heart rate.')}
        </p>
      )}

      {session.health_status === 'pending' && canSync && !unlinked && (
        <div className="mt-4 flex flex-wrap items-center gap-3">
          <button
            type="button"
            disabled={syncing}
            onClick={() =>
              startSync.mutate(
                { ...rangeForDays(2, today()), data_types: [...SYNC_DATA_TYPES] },
                { onSuccess: (r) => setRunId(r.run_id) },
              )
            }
            className={secondaryButton}
          >
            {syncing ? 'Syncing…' : 'Sync now'}
          </button>
          <span className={muted} role="status">
            {startSync.error
              ? apiErrorMessage(startSync.error, 'Could not start the sync.')
              : run.error
                ? apiErrorMessage(run.error, 'Lost track of the sync.')
                : syncDone
                  ? `Sync ${run.data?.status}.`
                  : ''}
          </span>
        </div>
      )}
    </section>
  );
}

const formOf = (s: WorkoutSession & { finished_at: string }) => ({
  name: s.name,
  note: s.note,
  started: toLocalInput(s.started_at),
  finished: toLocalInput(s.finished_at),
});

function EditForm({ session }: { session: WorkoutSession & { finished_at: string } }) {
  const initial = formOf(session);
  const [form, setForm] = useState(initial);
  const [timeError, setTimeError] = useState<string | null>(null);
  const [saved, setSaved] = useState(false);
  const patch = usePatchSession(session.id, (s) => {
    setSaved(true);
    // Show what the server stored (a moved time comes back to the minute).
    if (s.finished_at) setForm(formOf({ ...s, finished_at: s.finished_at }));
  });

  const submit = (e: FormEvent) => {
    e.preventDefault();
    setSaved(false);
    setTimeError(null);
    const body: WorkoutSessionPatch = {};
    if (form.name !== initial.name) body.name = form.name;
    if (form.note !== initial.note) body.note = form.note;
    // A time is sent only when it was edited: the field holds minutes, the
    // stored value has seconds.
    for (const [field, key] of [
      ['started', 'started_at'],
      ['finished', 'finished_at'],
    ] as const) {
      if (form[field] === initial[field]) continue;
      const iso = fromLocalInput(form[field]);
      if (!iso) {
        setTimeError('Enter a valid date and time.');
        return;
      }
      body[key] = iso;
    }
    if (Object.keys(body).length > 0) patch.mutate(body);
  };

  const field = (key: keyof typeof form) => ({
    value: form[key],
    onChange: (e: { target: { value: string } }) => {
      setSaved(false);
      setForm({ ...form, [key]: e.target.value });
    },
  });

  return (
    <form onSubmit={submit} className={cn(cardClass, 'grid grid-cols-1 gap-4 sm:grid-cols-2')}>
      <h2 className={cn(heading, 'sm:col-span-2')}>Details</h2>
      <div className="sm:col-span-2">
        <label htmlFor="session-name" className={labelClass}>
          Name
        </label>
        <input id="session-name" maxLength={120} className={inputClass} {...field('name')} />
      </div>
      <div>
        <label htmlFor="session-started" className={labelClass}>
          Started
        </label>
        <input
          id="session-started"
          type="datetime-local"
          required
          className={inputClass}
          {...field('started')}
        />
      </div>
      <div>
        <label htmlFor="session-finished" className={labelClass}>
          Finished
        </label>
        <input
          id="session-finished"
          type="datetime-local"
          required
          className={inputClass}
          {...field('finished')}
        />
      </div>
      <div className="sm:col-span-2">
        <label htmlFor="session-note" className={labelClass}>
          Note
        </label>
        <textarea
          id="session-note"
          rows={2}
          maxLength={2000}
          className={textareaClass}
          {...field('note')}
        />
      </div>
      <div className="flex flex-wrap items-center gap-3 sm:col-span-2">
        <button type="submit" disabled={patch.isPending} className={primaryButton}>
          {patch.isPending ? 'Saving…' : 'Save'}
        </button>
        <span role="status" className={muted}>
          {saved ? 'Saved. Changed times are matched with the band data again.' : ''}
        </span>
        {(timeError ?? patch.error) && (
          <span role="alert" className="text-theme-sm text-error-500">
            {timeError ?? patch.error}
          </span>
        )}
      </div>
    </form>
  );
}

function Detail({ session }: { session: WorkoutSession & { finished_at: string } }) {
  const navigate = useNavigate();
  const { queue, put, remove } = useSetQueue(session.id);
  const [correcting, setCorrecting] = useState(false);
  const [confirmDelete, setConfirmDelete] = useState(false);
  const del = useDeleteSession(session.id, () => navigate('/workout/history'));

  const shown: WorkoutSession = {
    ...session,
    exercises: session.exercises.map((e) => ({ ...e, sets: mergeSets(e.sets, queue, e.id) })),
  };

  return (
    <>
      <section className={cardClass} aria-label="Summary">
        <p className={muted}>
          {formatDay(session.started_at)}, {formatTime(session.started_at)}–
          {formatTime(session.finished_at)}
        </p>
        <div className="mt-3 grid grid-cols-2 gap-4 sm:grid-cols-4">
          <Stat
            label="Duration"
            value={formatDuration(durationMinutes(session.started_at, session.finished_at))}
          />
          <Stat label="Work sets" value={String(workSetCount(shown))} />
          <Stat label="Volume" value={formatVolume(sessionVolume(shown))} />
          <Stat
            label="Body weight"
            value={session.bodyweight_kg == null ? '–' : `${session.bodyweight_kg.toFixed(1)} kg`}
            note="at the start"
          />
        </div>
        {session.note && (
          <p className="mt-3 text-theme-sm whitespace-pre-line text-gray-700 dark:text-gray-300">
            {session.note}
          </p>
        )}
      </section>

      <HealthBlock session={session} />

      <div className="flex flex-wrap items-center justify-between gap-3">
        <h2 className={heading}>Exercises</h2>
        <button
          type="button"
          onClick={() => setCorrecting(!correcting)}
          aria-pressed={correcting}
          className={secondaryButton}
        >
          {correcting ? 'Done correcting' : 'Correct sets'}
        </button>
      </div>

      {shown.exercises.length === 0 ? (
        <p className={cn(cardClass, muted)}>No exercises were logged.</p>
      ) : correcting ? (
        session.exercises.map((e) => (
          <ExerciseLog key={e.id} exercise={e} queue={queue} onPut={put} onRemove={remove} />
        ))
      ) : (
        <ul className="flex flex-col gap-3">
          {shown.exercises.map((e) => {
            let work = 0;
            return (
              <li key={e.id} className={cardClass}>
                <h3 className="text-base font-semibold text-gray-800 dark:text-white/90">
                  {e.exercise_name}
                </h3>
                {e.sets.length === 0 ? (
                  <p className={cn(muted, 'mt-1')}>No sets.</p>
                ) : (
                  <ol className="mt-2 flex flex-wrap gap-2">
                    {e.sets.map((s) => (
                      <li
                        key={s.id}
                        className="rounded-lg bg-gray-100 px-3 py-1.5 text-theme-sm text-gray-800 tabular-nums dark:bg-white/5 dark:text-white/90"
                      >
                        <span className="me-1.5 text-gray-500 dark:text-gray-400">
                          {s.kind === 'warmup' ? 'W' : ++work}
                        </span>
                        {describeSet(s, e.tracking_mode)}
                      </li>
                    ))}
                  </ol>
                )}
              </li>
            );
          })}
        </ul>
      )}

      <EditForm session={session} />

      <div className="flex justify-end">
        <button type="button" onClick={() => setConfirmDelete(true)} className={dangerButton}>
          Delete workout
        </button>
      </div>
      {del.error && (
        <p role="alert" className="text-theme-sm text-error-500">
          {del.error}
        </p>
      )}
      {confirmDelete && (
        <ConfirmDialog
          title="Delete this workout?"
          description="The workout and its sets are deleted for good."
          confirmLabel="Delete"
          destructive
          busy={del.isPending}
          onConfirm={() => del.mutate()}
          onClose={() => setConfirmDelete(false)}
        />
      )}
    </>
  );
}

/** One workout from the history: totals, the Health block, sets and corrections. */
export function WorkoutHistoryDetailPage() {
  const { id = '' } = useParams();
  const q = useSession(id);
  const session = q.data;

  return (
    <WorkoutFrame title={session ? session.name || 'Workout' : 'Workout'}>
      {q.isPending ? (
        <div
          className="h-64 animate-pulse rounded-2xl bg-gray-100 dark:bg-white/5"
          aria-label="Loading"
        />
      ) : q.error || !session ? (
        <LoadError error={q.error} onRetry={() => q.refetch()} />
      ) : session.finished_at === null ? (
        <div className={cardClass}>
          <p className={muted}>This workout is still in progress.</p>
          <Link to="/workout/session" className={cn(primaryButton, 'mt-4')}>
            Continue
          </Link>
        </div>
      ) : (
        <Detail key={session.id} session={{ ...session, finished_at: session.finished_at }} />
      )}
    </WorkoutFrame>
  );
}
