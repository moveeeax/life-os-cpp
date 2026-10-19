import { useState } from 'react';

import { useMood, useSaveMood } from '@/hooks/useHabits';
import {
  MOOD_TAGS,
  longDay,
  moodBody,
  moodForm,
  moodOf,
  moodWord,
  tagLine,
  toggleTag,
  type MoodCheckin,
} from '@/lib/habits';
import { Group, Num, Row, Sheet } from '@/shell/kit';

/** The mood of a day: a score from 1 to 5, tags from the fixed set, a note. */
export function MoodSheet({
  date,
  mood,
  onClose,
}: {
  date: string;
  mood?: MoodCheckin;
  onClose: () => void;
}) {
  const [f, setF] = useState(() => moodForm(mood));
  const save = useSaveMood(onClose);
  const body = moodBody(f);
  return (
    <Sheet title="Mood" onClose={onClose} width={440}>
      <p className="k-sub" data-num-ok>
        {longDay(date)}
      </p>
      <form
        className="k-form"
        onSubmit={(e) => {
          e.preventDefault();
          if (body) save.mutate({ date, body });
        }}
      >
        <div className="k-field">
          <span id="mood-score">How was it</span>
          <div className="k-seg h-score" role="radiogroup" aria-labelledby="mood-score">
            {[1, 2, 3, 4, 5].map((s) => (
              <button
                key={s}
                type="button"
                role="radio"
                aria-checked={f.score === s}
                aria-label={`${s}, ${moodWord(s)}`}
                className={f.score === s ? 'k-seg-on' : undefined}
                onClick={() => setF({ ...f, score: s })}
              >
                <Num>{s}</Num>
              </button>
            ))}
          </div>
          {f.score != null && <span className="k-muted">{moodWord(f.score)}</span>}
        </div>
        <div className="k-field">
          <span id="mood-tags">Tags</span>
          <div className="h-tags" role="group" aria-labelledby="mood-tags">
            {MOOD_TAGS.map((t) => (
              <button
                key={t}
                type="button"
                className="k-btn k-chip h-tag"
                aria-pressed={f.tags.includes(t)}
                onClick={() => setF({ ...f, tags: toggleTag(f.tags, t) })}
              >
                {t}
              </button>
            ))}
          </div>
        </div>
        <label className="k-field">
          <span>Note (optional)</span>
          <input
            maxLength={500}
            placeholder="What made the day"
            value={f.note}
            onChange={(e) => setF({ ...f, note: e.target.value })}
          />
        </label>
        {save.error && (
          <p role="alert" className="k-error">
            {save.error}
          </p>
        )}
        <div className="k-dlg-actions">
          {mood && (
            <button
              type="button"
              className="k-link k-error-text"
              disabled={save.isPending}
              onClick={() => save.mutate({ date, body: null })}
            >
              Remove
            </button>
          )}
          <span className="k-grow" />
          <button type="button" className="k-btn" onClick={onClose}>
            Cancel
          </button>
          <button type="submit" className="k-btn k-solid" disabled={!body || save.isPending}>
            {save.isPending ? 'Saving…' : 'Save'}
          </button>
        </div>
      </form>
    </Sheet>
  );
}

/** Today's mood as one calm row on the Habits page; the check-in opens a sheet. */
export function MoodToday({ today }: { today: string }) {
  const q = useMood(today, today);
  const [open, setOpen] = useState(false);
  const mood = moodOf(q.data, today);
  if (q.isPending || q.isError) return null;
  return (
    <Group label="Mood">
      <Row
        title={mood ? moodWord(mood.score) : 'Not checked in today'}
        sub={mood && mood.tags.length > 0 ? tagLine(mood.tags) : undefined}
        right={
          mood ? (
            <span>
              <Num>{mood.score}</Num> of <Num>5</Num>
            </span>
          ) : undefined
        }
        action={
          <button type="button" className="k-btn h-mood-btn" onClick={() => setOpen(true)}>
            {mood ? 'Edit' : 'Check in'}
          </button>
        }
      >
        {mood?.note ? <span>{mood.note}</span> : undefined}
      </Row>
      {open && <MoodSheet date={today} mood={mood} onClose={() => setOpen(false)} />}
    </Group>
  );
}
