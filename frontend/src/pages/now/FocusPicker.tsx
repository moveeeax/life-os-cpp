import { useState } from 'react';

import { useSetFocus } from '@/hooks/useNow';
import type { Today } from '@/lib/now';

interface Pick {
  key: string;
  kind: 'task' | 'goal';
  id: string;
  title: string;
}

function picksOf(today: Today | undefined): Pick[] {
  const out: Pick[] = [];
  const seen = new Set<string>();
  const add = (p: Pick) => {
    if (!seen.has(p.key)) {
      seen.add(p.key);
      out.push(p);
    }
  };
  const late = today?.tasks?.first_late;
  if (late) add({ key: `t${late.id}`, kind: 'task', id: late.id, title: late.title });
  for (const t of today?.tasks?.today ?? [])
    add({ key: `t${t.id}`, kind: 'task', id: t.id, title: t.title });
  for (const g of today?.goals ?? []) {
    if (g.pace === 'behind') add({ key: `g${g.id}`, kind: 'goal', id: g.id, title: g.title });
  }
  return out.slice(0, 6);
}

/** "What is your main focus for today?": a task, a goal behind, or typed text. */
export function FocusPicker({
  date,
  today,
  onDone,
}: {
  date: string;
  today: Today | undefined;
  onDone: () => void;
}) {
  const set = useSetFocus(onDone);
  const [text, setText] = useState('');
  const picks = picksOf(today);

  return (
    <>
      <label className="n-hello" htmlFor="focus-text">
        What is your main focus for today?
      </label>
      <input
        id="focus-text"
        className="n-input"
        value={text}
        maxLength={200}
        placeholder="Something else"
        autoComplete="off"
        onChange={(e) => setText(e.target.value)}
        onKeyDown={(e) => {
          if (e.key === 'Enter' && text.trim())
            set.mutate({ date, kind: 'text', text: text.trim() });
        }}
      />
      {picks.length > 0 && (
        <div className="n-picks">
          {picks.map((p) => (
            <button
              key={p.key}
              type="button"
              className="n-pick"
              disabled={set.isPending}
              onClick={() => set.mutate({ date, kind: p.kind, ref_id: p.id })}
            >
              {p.title}
            </button>
          ))}
        </div>
      )}
      {set.error && <span className="n-gone">{set.error}</span>}
    </>
  );
}
