import { useEffect, useState } from 'react';

import { useSaveNote } from '@/hooks/useNow';

/** The note of the day under the focus, saved when the field loses focus; yesterday's in one line. */
export function NoteLine({
  date,
  today,
  yesterday,
}: {
  date: string;
  today: string | null;
  yesterday: string | null;
}) {
  const [text, setText] = useState(today ?? '');
  const save = useSaveNote();
  // A new day, or the note saved in another tab, replaces the field while it is not being edited.
  useEffect(() => setText(today ?? ''), [today, date]);
  return (
    <div className="n-note-box">
      <textarea
        className="n-note-field"
        aria-label="Note for today"
        placeholder="Note for today"
        maxLength={2000}
        rows={text.includes('\n') ? 3 : 1}
        value={text}
        onChange={(e) => setText(e.target.value)}
        onBlur={() => {
          if (text.trim() !== (today ?? '').trim()) save.mutate({ date, text });
        }}
      />
      {save.error && <span className="n-note">The note was not saved: {save.error}</span>}
      {yesterday && <span className="n-yesterday">Yesterday: {yesterday}</span>}
    </div>
  );
}
