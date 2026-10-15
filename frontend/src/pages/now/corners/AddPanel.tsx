import { useState } from 'react';

import { Capture as TasksCapture } from '@/pages/tasks/Capture';

import { Capture } from './Capture';

type Kind = 'task' | 'meal' | 'spend';

const LABEL: Record<Kind, string> = { task: 'Task', meal: 'Meal', spend: 'Spend' };

/**
 * The Add corner: a task, a meal or a spend in one line of text. A task goes
 * through the Tasks capture (the phrase names its date, today without one);
 * a meal and a spend through the Food and Money parses.
 */
export function AddPanel({ date, hour }: { date: string; hour: number }) {
  const [kind, setKind] = useState<Kind>('task');
  return (
    <>
      <h2>Add</h2>
      <div className="c-capture-kind" role="group" aria-label="What to add">
        {(['task', 'meal', 'spend'] as const).map((k) => (
          <button
            key={k}
            type="button"
            aria-pressed={kind === k}
            className={kind === k ? 'c-on' : undefined}
            onClick={() => setKind(k)}
          >
            {LABEL[k]}
          </button>
        ))}
      </div>
      {kind === 'task' ? (
        <div className="c-capture">
          <TasksCapture defaultDue={date} autoFocus />
        </div>
      ) : (
        <Capture date={date} hour={hour} kind={kind} />
      )}
    </>
  );
}
