import { useState } from 'react';

import { useSetFocus } from '@/hooks/useNow';

/** "What is your main focus for today?": typed text, saved on Enter. */
export function FocusPicker({ date, onDone }: { date: string; onDone: () => void }) {
  const set = useSetFocus(onDone);
  const [text, setText] = useState('');

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
        autoComplete="off"
        onChange={(e) => setText(e.target.value)}
        onKeyDown={(e) => {
          if (e.key === 'Enter' && text.trim())
            set.mutate({ date, kind: 'text', text: text.trim() });
        }}
      />
      {set.error && <span className="n-gone">{set.error}</span>}
    </>
  );
}
