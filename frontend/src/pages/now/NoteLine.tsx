import { useEffect, useRef, useState } from 'react';

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
  const editing = useRef(false);
  // The day the edit began: a note typed before midnight is saved to that day.
  const editDate = useRef(date);
  const save = useSaveNote();
  // The server's note replaces the field only while nobody is typing in it.
  useEffect(() => {
    if (!editing.current) setText(today ?? '');
  }, [today, date]);
  return (
    <div className="n-note-box">
      <textarea
        className="n-note-field"
        aria-label="Note for today"
        placeholder="Note for today"
        maxLength={2000}
        rows={text.includes('\n') ? 3 : 1}
        value={text}
        onFocus={() => {
          editing.current = true;
          editDate.current = date;
        }}
        onChange={(e) => setText(e.target.value)}
        onBlur={() => {
          editing.current = false;
          if (text.trim() !== (editDate.current === date ? (today ?? '') : '').trim())
            save.mutate({ date: editDate.current, text });
        }}
      />
      {save.error && (
        <span role="alert" className="n-note">
          The note was not saved: {save.error}
        </span>
      )}
      {yesterday && <span className="n-yesterday">Yesterday: {yesterday}</span>}
    </div>
  );
}
