import { useState } from 'react';
import { Check, ChevronLeft, ChevronRight, Plus } from 'lucide-react';
import { Link, useNavigate } from 'react-router';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { Modal } from '@/components/Modal';
import {
  useActiveSession,
  useAddSessionExercise,
  useDeleteSession,
  useNow,
  usePatchSession,
  useRemoveSessionExercise,
  useRestTimer,
  useSetQueue,
} from '@/hooks/useWorkoutSession';
import { formatVolume } from '@/lib/workout/format';
import { formatClock, mergeSets, sessionVolume, workSetCount } from '@/lib/workout/session';
import type { WorkoutSession } from '@/lib/workout/types';
import { cn } from '@/lib/utils';

import { Stat } from './bits';
import { ExerciseBrowser } from './ExerciseBrowser';
import { ExerciseLog } from './ExerciseLog';
import { LoadError, WorkoutFrame } from './frame';
import { cardClass, dangerButton, modalPanel, primaryButton, secondaryButton } from './styles';

type Confirm = 'finish' | 'discard' | 'remove' | null;

interface RestBarProps {
  rest: ReturnType<typeof useRestTimer>;
}

/** The countdown between sets; stays in view at the bottom of the page. */
function RestBar({ rest }: RestBarProps) {
  if (!rest.running && !rest.justEnded) return null;
  return (
    <div
      role="status"
      className={cn(
        'sticky bottom-3 z-20 flex items-center gap-3 rounded-2xl px-4 py-3 text-white shadow-theme-lg',
        rest.running ? 'bg-gray-900 dark:bg-gray-700' : 'animate-pulse bg-success-600',
      )}
    >
      {rest.running ? (
        <>
          <span className="text-theme-sm">Rest</span>
          <span className="flex-1 text-2xl font-semibold tabular-nums">
            {formatClock(rest.remaining)}
          </span>
          <button
            type="button"
            onClick={() => rest.add(30)}
            className="rounded-lg border border-white/30 px-3 py-2 text-theme-sm font-medium hover:bg-white/10"
          >
            +30 s
          </button>
          <button
            type="button"
            onClick={rest.stop}
            className="rounded-lg border border-white/30 px-3 py-2 text-theme-sm font-medium hover:bg-white/10"
          >
            Skip
          </button>
        </>
      ) : (
        <span className="flex-1 text-lg font-semibold">Rest is over. Next set.</span>
      )}
    </div>
  );
}

/** Where a reload lands: the first exercise that still has sets to do. */
function firstOpenExercise(session: WorkoutSession): number {
  const open = session.exercises.findIndex((e) => e.sets.length < (e.target_sets ?? 1));
  return open === -1 ? Math.max(0, session.exercises.length - 1) : open;
}

function ActiveSession({ session }: { session: WorkoutSession }) {
  const navigate = useNavigate();
  const { queue, put, remove } = useSetQueue(session.id);
  const rest = useRestTimer();
  const now = useNow(1000);

  const [position, setPosition] = useState(() => firstOpenExercise(session));
  const [picking, setPicking] = useState(false);
  const [confirm, setConfirm] = useState<Confirm>(null);

  const count = session.exercises.length;
  const index = Math.min(position, Math.max(0, count - 1));
  const exercise = count > 0 ? session.exercises[index] : null;

  // What the user sees, unsent sets included.
  const shown: WorkoutSession = {
    ...session,
    exercises: session.exercises.map((e) => ({ ...e, sets: mergeSets(e.sets, queue, e.id) })),
  };
  const unsent = queue.filter((q) => q.sessionId === session.id).length;

  const finish = usePatchSession(session.id, (s) => navigate(`/workout/history/${s.id}`));
  const discard = useDeleteSession(session.id, () => navigate('/workout'));
  const addExercise = useAddSessionExercise(session.id, (s) => {
    setPosition(s.exercises.length - 1);
    setPicking(false);
  });
  const removeExercise = useRemoveSessionExercise(session.id, () => setConfirm(null));
  const error = finish.error ?? discard.error ?? addExercise.error ?? removeExercise.error;

  return (
    <div className="flex flex-col gap-4">
      <div className={cardClass}>
        <div className="flex flex-wrap items-center justify-between gap-3">
          <h2 className="text-lg font-semibold text-gray-800 dark:text-white/90">
            {session.name || 'Workout'}
          </h2>
          <button type="button" onClick={() => setConfirm('finish')} className={primaryButton}>
            <Check className="size-4" aria-hidden="true" />
            Finish
          </button>
        </div>
        <div className="mt-3 grid grid-cols-3 gap-3">
          <Stat label="Time" value={formatClock((now - Date.parse(session.started_at)) / 1000)} />
          <Stat label="Work sets" value={String(workSetCount(shown))} />
          <Stat label="Volume" value={formatVolume(sessionVolume(shown))} />
        </div>
        {unsent > 0 && (
          <p className="mt-3 text-theme-sm text-warning-600 dark:text-orange-400" role="status">
            {unsent} {unsent === 1 ? 'set is' : 'sets are'} not saved on the server yet: kept on
            this device and sent again automatically.
          </p>
        )}
      </div>

      {count > 0 && (
        <nav aria-label="Exercises of this workout" className="flex gap-2 overflow-x-auto pb-1">
          {shown.exercises.map((e, i) => {
            const done = e.sets.length >= (e.target_sets ?? 1) && e.sets.length > 0;
            return (
              <button
                key={e.id}
                type="button"
                onClick={() => setPosition(i)}
                aria-current={i === index ? 'step' : undefined}
                className={cn(
                  'flex shrink-0 items-center gap-1.5 rounded-full border px-3 py-1.5 text-theme-sm font-medium whitespace-nowrap transition',
                  i === index
                    ? 'border-brand-500 bg-brand-500 text-white'
                    : 'border-gray-300 text-gray-700 hover:bg-gray-100 dark:border-gray-700 dark:text-gray-300 dark:hover:bg-white/5',
                )}
              >
                {done && <Check className="size-3.5" aria-label="done" />}
                {i + 1}. {e.exercise_name}
              </button>
            );
          })}
        </nav>
      )}

      {exercise ? (
        <ExerciseLog
          key={exercise.id}
          exercise={exercise}
          queue={queue}
          onPut={put}
          onRemove={remove}
          onLogged={() => rest.start(exercise.rest_seconds)}
        />
      ) : (
        <p className={cn(cardClass, 'text-theme-sm text-gray-500 dark:text-gray-400')}>
          This workout has no exercises yet. Add the first one.
        </p>
      )}

      {count > 1 && (
        <div className="grid grid-cols-2 gap-3">
          <button
            type="button"
            onClick={() => setPosition(index - 1)}
            disabled={index === 0}
            className={secondaryButton}
          >
            <ChevronLeft className="size-4" aria-hidden="true" />
            Previous
          </button>
          <button
            type="button"
            onClick={() => setPosition(index + 1)}
            disabled={index === count - 1}
            className={secondaryButton}
          >
            Next
            <ChevronRight className="size-4" aria-hidden="true" />
          </button>
        </div>
      )}

      <div className="flex flex-wrap gap-3">
        <button type="button" onClick={() => setPicking(true)} className={secondaryButton}>
          <Plus className="size-4" aria-hidden="true" />
          Add exercise
        </button>
        {exercise && (
          <button type="button" onClick={() => setConfirm('remove')} className={secondaryButton}>
            Remove this exercise
          </button>
        )}
        <button
          type="button"
          onClick={() => setConfirm('discard')}
          className={cn(dangerButton, 'ms-auto')}
        >
          Discard workout
        </button>
      </div>

      {error && (
        <p role="alert" className="text-theme-sm text-error-500">
          {error}
        </p>
      )}

      <RestBar rest={rest} />

      {picking && (
        <Modal onClose={() => setPicking(false)} className="max-w-5xl">
          <div className={modalPanel}>
            <div className="mb-4 flex items-center justify-between gap-3">
              <h2 className="text-lg font-semibold">Add exercise</h2>
              <button type="button" onClick={() => setPicking(false)} className={secondaryButton}>
                Close
              </button>
            </div>
            <ExerciseBrowser onSelect={(e) => addExercise.mutate(e.id)} />
          </div>
        </Modal>
      )}
      {confirm === 'finish' && (
        <ConfirmDialog
          title="Finish the workout?"
          description={
            unsent > 0
              ? `${unsent} unsent ${unsent === 1 ? 'set stays' : 'sets stay'} queued on this device and will be sent when the connection is back.`
              : 'The workout moves to the history. Its sets can still be corrected there.'
          }
          confirmLabel="Finish"
          busy={finish.isPending}
          onConfirm={() => {
            rest.stop();
            finish.mutate({ finish: true });
          }}
          onClose={() => setConfirm(null)}
        />
      )}
      {confirm === 'discard' && (
        <ConfirmDialog
          title="Discard the workout?"
          description="The workout and every set logged in it are deleted."
          confirmLabel="Discard"
          destructive
          busy={discard.isPending}
          onConfirm={() => {
            rest.stop();
            discard.mutate();
          }}
          onClose={() => setConfirm(null)}
        />
      )}
      {confirm === 'remove' && exercise && (
        <ConfirmDialog
          title={`Remove ${exercise.exercise_name}?`}
          description="The exercise and its sets leave this workout. The routine is not changed."
          confirmLabel="Remove"
          destructive
          busy={removeExercise.isPending}
          onConfirm={() => removeExercise.mutate(exercise.id)}
          onClose={() => setConfirm(null)}
        />
      )}
    </div>
  );
}

/** The workout in progress. Built for a phone held in one hand. */
export function WorkoutSessionPage() {
  const active = useActiveSession();

  return (
    <WorkoutFrame title="Workout">
      {active.isPending ? (
        <div
          className="h-64 animate-pulse rounded-2xl bg-gray-100 dark:bg-white/5"
          aria-label="Loading"
        />
      ) : active.error ? (
        <LoadError error={active.error} onRetry={() => active.refetch()} />
      ) : active.data ? (
        <ActiveSession key={active.data.id} session={active.data} />
      ) : (
        <div className={cardClass}>
          <p className="text-theme-sm text-gray-500 dark:text-gray-400">
            No workout is in progress.
          </p>
          <Link to="/workout" className={cn(primaryButton, 'mt-4')}>
            Start one
          </Link>
        </div>
      )}
    </WorkoutFrame>
  );
}
