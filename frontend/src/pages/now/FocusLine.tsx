import { useEffect, useState } from 'react';

import { useFocusDone } from '@/hooks/useNow';
import type { Focus } from '@/lib/now';

/** The day's one focus: tick it (a task closes with it), or change it. */
export function FocusLine({ focus, onChange }: { focus: Focus; onChange: () => void }) {
  const done = useFocusDone();
  const [ticked, setTicked] = useState(focus.done);
  useEffect(() => setTicked(focus.done), [focus.done]);

  const toggle = () => {
    const next = !ticked;
    setTicked(next); // at once; the server answer follows
    done.mutate({ date: focus.date, done: next }, { onError: () => setTicked(!next) });
  };

  return (
    <>
      <div className={ticked ? 'n-line n-done' : 'n-line'}>
        <button
          type="button"
          className="n-ring"
          aria-pressed={ticked}
          aria-label={`Done: ${focus.text}`}
          disabled={focus.missing || done.isPending}
          onClick={toggle}
        />
        <span className="n-text" data-num-ok>
          {focus.text}
        </span>
      </div>
      {focus.missing && (
        <span className="n-gone">This {focus.kind} is gone. Pick another focus.</span>
      )}
      {done.error && <span className="n-gone">{done.error}</span>}
      <button type="button" className="n-change" onClick={onChange}>
        Change
      </button>
    </>
  );
}
