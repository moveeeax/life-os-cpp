import { useState } from 'react';
import { Link } from 'react-router';

import { useNoteList } from '@/hooks/useNotes';
import { noteDay, type Note } from '@/lib/notes';
import { Group, Num, Row } from '@/shell/kit';

import { LoadError, NotesFrame, Placeholder } from './frame';
import { NoteSheet } from './NoteSheet';

function NoteRow({ n }: { n: Note }) {
  const date = noteDay(n).day;
  return (
    <Row
      give={{ kind: 'note', id: n.id, title: n.title }}
      dot={
        n.project_kind === 'course'
          ? 'var(--g-ok)'
          : n.project_id
            ? 'var(--g-brand)'
            : 'var(--g-ink-faint)'
      }
      title={
        <Link to={`/notes/${n.id}`} className="k-row-link" data-num-ok>
          {n.title}
        </Link>
      }
      sub={
        <span data-num-ok>
          {[date, n.topic, n.tags.join(', ')].filter(Boolean).join(' · ')}
          {n.project_title && <span className="k-narrow"> · {n.project_title}</span>}
        </span>
      }
      right={
        n.project_title ? (
          <span className="k-wide" data-num-ok>
            {n.project_title}
          </span>
        ) : undefined
      }
    />
  );
}

/** The notes by their day, newest first (the day a note is about when set): search, topic chips, grouped by month. */
export function NotesPage() {
  const [q, setQ] = useState('');
  const [topic, setTopic] = useState('');
  const [adding, setAdding] = useState(false);
  const notes = useNoteList({ q: q.trim(), topic });
  const data = notes.data;
  const months = new Map<string, Note[]>();
  for (const n of data?.data ?? []) {
    const m = noteDay(n).month;
    months.set(m, [...(months.get(m) ?? []), n]);
  }
  return (
    <NotesFrame
      headline={
        <>
          <Num>{data?.total ?? 0}</Num> {data?.total === 1 ? 'note' : 'notes'}
        </>
      }
      sub="private for now"
      tools={
        <>
          <input
            className="k-input k-search"
            type="search"
            aria-label="Search notes"
            placeholder="Search notes"
            value={q}
            onChange={(e) => setQ(e.target.value)}
          />
          {(data?.topics.length ?? 0) > 0 && (
            <div className="k-seg k-seg-scroll" role="group" aria-label="Topic">
              <button
                type="button"
                aria-pressed={topic === ''}
                className={topic === '' ? 'k-seg-on' : undefined}
                onClick={() => setTopic('')}
              >
                All
              </button>
              {data!.topics.map((t) => (
                <button
                  key={t.topic}
                  type="button"
                  aria-pressed={topic === t.topic}
                  className={topic === t.topic ? 'k-seg-on' : undefined}
                  onClick={() => setTopic(topic === t.topic ? '' : (t.topic ?? ''))}
                  data-num-ok
                >
                  {t.topic}
                </button>
              ))}
            </div>
          )}
          <span className="k-grow" />
          <button type="button" className="k-btn k-solid" onClick={() => setAdding(true)}>
            New note
          </button>
        </>
      }
    >
      {notes.isPending ? (
        <Placeholder />
      ) : notes.isError ? (
        <LoadError error={notes.error} onRetry={() => void notes.refetch()} />
      ) : (data?.data.length ?? 0) === 0 ? (
        <p className="k-empty">
          {q || topic ? 'No note matches.' : 'No notes yet. They stay private for now.'}
        </p>
      ) : (
        [...months.entries()].map(([month, list]) => (
          <Group key={month} label={month}>
            {list.map((n) => (
              <NoteRow key={n.id} n={n} />
            ))}
          </Group>
        ))
      )}
      {adding && <NoteSheet onClose={() => setAdding(false)} />}
    </NotesFrame>
  );
}
