import { useState } from 'react';
import { Check, ChevronLeft, ChevronRight, Plus } from 'lucide-react';
import { useNavigate } from 'react-router';

import { ConfirmDialog } from '@/components/ConfirmDialog';
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
import { Banner, Sheet } from '@/shell/kit';
import { dangerButton, primaryButton, secondaryButton } from '@/shell/kit/styles';

type Confirm = 'finish' | 'discard' | 'remove' | null;

interface RestBarProps {
  rest: ReturnType<typeof useRestTimer>;
}

/** The countdown between sets; stays in view at the bottom of the page. */
function RestBar({ rest }: RestBarProps) {
  if (!rest.running && !rest.justEnded) return null;
  return (
    <div role="status" className={cn('k-rest', !rest.running && 'k-rest-done')}>
      {rest.running ? (
        <>
          <span className="k-muted">Rest</span>
          <span className="k-rest-clock">{formatClock(rest.remaining)}</span>
          <button type="button" onClick={() => rest.add(30)} className="k-btn">
            +30 s
          </button>
          <button type="button" onClick={rest.stop} className="k-btn">
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
    <div className="k-form">
      <div className="k-sticky">
        <div className="k-tools k-tools-tight">
          <span className="k-row-title">{session.name || 'Workout'}</span>
          <button type="button" onClick={() => setConfirm('finish')} className={primaryButton}>
            <Check className="size-4" aria-hidden="true" />
            Finish
          </button>
        </div>
        <div className="k-facts">
          <Stat label="Time" value={formatClock((now - Date.parse(session.started_at)) / 1000)} />
          <Stat label="Work sets" value={String(workSetCount(shown))} />
          <Stat label="Volume" value={formatVolume(sessionVolume(shown))} />
        </div>
        {unsent > 0 && (
          <p className="k-sub k-warn-text" role="status">
            {unsent} {unsent === 1 ? 'set is' : 'sets are'} not saved on the server yet: kept on
            this device and sent again automatically.
          </p>
        )}
      </div>

      {count > 0 && (
        <nav aria-label="Exercises of this workout" className="k-chips">
          {shown.exercises.map((e, i) => {
            const done = e.sets.length >= (e.target_sets ?? 1) && e.sets.length > 0;
            return (
              <button
                key={e.id}
                type="button"
                onClick={() => setPosition(i)}
                aria-current={i === index ? 'step' : undefined}
                className={i === index ? 'k-btn k-solid' : 'k-btn'}
              >
                {done && <Check className="size-3.5" aria-label="done" />}
                <span data-num-ok>{i + 1}.</span> {e.exercise_name}
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
        <p className="k-empty">This workout has no exercises yet. Add the first one.</p>
      )}

      {count > 1 && (
        <div className="k-fields">
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

      <div className="k-tools">
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
          className={cn(dangerButton, 'k-push')}
        >
          Discard workout
        </button>
      </div>

      {error && (
        <p role="alert" className="k-error">
          {String(error)}
        </p>
      )}

      <RestBar rest={rest} />

      {picking && (
        <Sheet title="Add exercise" onClose={() => setPicking(false)} width={720}>
          <ExerciseBrowser onSelect={(e) => addExercise.mutate(e.id)} />
        </Sheet>
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
    <WorkoutFrame title="Workout in progress">
      {active.isPending ? (
        <div className="k-placeholder" aria-label="Loading" />
      ) : active.error ? (
        <LoadError error={active.error} onRetry={() => active.refetch()} />
      ) : active.data ? (
        <ActiveSession key={active.data.id} session={active.data} />
      ) : (
        <Banner to="/workout" action="Start one">
          No workout is in progress.
        </Banner>
      )}
    </WorkoutFrame>
  );
}
