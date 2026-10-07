import { useEffect, useState, type FormEvent } from 'react';

import {
  useAcceptTasksParse,
  useCreateTask,
  useTasksParse,
  useTasksStatus,
  useUpdateNote,
} from '@/hooks/useTasks';
import { areaOf, dueLabel, EFFORT_LABEL, localToday, type TaskParseLine } from '@/lib/tasks';

const PLACEHOLDER = 'завтра написать в Kaspi про списание 2790';

function lineMeta(line: TaskParseLine, today: string): string {
  const parts: string[] = [areaOf(line.area).label];
  if (line.effort) parts.push(EFFORT_LABEL[line.effort]);
  const due = dueLabel(line.due ?? null, today);
  parts.push(due ? `due ${due.text}` : 'no date');
  if (line.next_step) parts.push(`next: ${line.next_step}`);
  return parts.join(' · ');
}

/**
 * One line: Enter starts the parse and the draft shows under it with Add and
 * Discard. Without a configured parse, Enter saves the phrase as typed.
 */
export function Capture({
  initial,
  noteId,
  onDone,
  autoFocus,
  autoStart,
}: {
  initial?: string;
  noteId?: string;
  onDone?: () => void;
  autoFocus?: boolean;
  /** Read `initial` at once (a note turned into a task). */
  autoStart?: boolean;
}) {
  const status = useTasksStatus().data;
  const parse = useTasksParse();
  const [text, setText] = useState(initial ?? '');
  const [lines, setLines] = useState<TaskParseLine[] | null>(null);
  const today = localToday();
  const finish = () => {
    setLines(null);
    setText('');
    parse.cancel();
    onDone?.();
  };
  const archive = useUpdateNote();
  // A note saved as typed leaves the inbox too (the parse accept does this on the server).
  const create = useCreateTask(() => {
    if (noteId) archive.mutate({ id: noteId, status: 'archived' });
    finish();
  });
  const accept = useAcceptTasksParse(parse.job?.id, finish);
  const plain = status?.llm_available === false;

  useEffect(() => {
    if (parse.state === 'done' && parse.job?.result) setLines(parse.job.result);
  }, [parse.state, parse.job]);

  const { start } = parse;
  useEffect(() => {
    if (autoStart && initial?.trim() && status?.llm_available) void start(initial.trim(), noteId);
  }, [autoStart, initial, noteId, start, status?.llm_available]);

  const asTyped = () => {
    const t = text.trim();
    if (t) create.mutate({ title: t.slice(0, 200), area: 'projects' });
  };

  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (!text.trim()) return;
    if (plain) {
      asTyped();
      return;
    }
    setLines(null);
    void parse.start(text.trim(), noteId);
  };

  const busy = parse.state === 'starting' || parse.state === 'running';
  return (
    <div className="grid gap-2.5">
      <form
        onSubmit={submit}
        className="flex items-center gap-2.5 border-b border-gray-200 pb-3 dark:border-white/10"
      >
        <span className="text-xl leading-none text-brand-500" aria-hidden="true">
          +
        </span>
        <input
          value={text}
          onChange={(e) => setText(e.target.value)}
          placeholder={PLACEHOLDER}
          aria-label="New task in one phrase"
          autoComplete="off"
          autoFocus={autoFocus}
          disabled={busy}
          className="min-w-0 flex-1 bg-transparent py-1.5 text-theme-sm text-gray-800 outline-none placeholder:text-gray-400 dark:text-white/90"
        />
      </form>
      {plain && (
        <p className="text-theme-xs text-gray-500">
          Typed tasks are saved as written: the phrase reader is not set up.
        </p>
      )}
      {busy && <p className="text-theme-sm text-gray-500">Reading…</p>}
      {parse.state === 'failed' && (
        <p role="alert" className="text-theme-sm text-error-500">
          {parse.error === 'not_configured'
            ? 'The phrase reader is not set up.'
            : `Could not read the phrase (${parse.error ?? 'unknown error'}).`}{' '}
          <button type="button" onClick={asTyped} className="font-medium text-brand-500">
            Add as typed
          </button>
        </p>
      )}
      {lines && (
        <div className="grid gap-2">
          {lines.map((l, i) => (
            <div
              key={i}
              className="grid gap-0.5 rounded-lg bg-brand-50 px-3.5 py-3 dark:bg-brand-500/15"
            >
              <p className="text-theme-sm font-medium text-gray-800 dark:text-white/90">
                {l.title}
              </p>
              <p className="text-theme-xs text-gray-500">{lineMeta(l, today)}</p>
              {l.possible_duplicate && (
                <p className="text-theme-xs text-warning-600">An open task has the same title.</p>
              )}
            </div>
          ))}
          <div className="flex gap-3.5 text-theme-sm">
            <button
              type="button"
              disabled={accept.isPending}
              onClick={() => accept.mutate(lines)}
              className="font-medium text-brand-500"
            >
              {accept.isPending ? 'Adding…' : lines.length > 1 ? `Add ${lines.length}` : 'Add'}
            </button>
            <button
              type="button"
              onClick={() => {
                setLines(null);
                parse.cancel();
              }}
              className="text-gray-500"
            >
              Discard
            </button>
          </div>
        </div>
      )}
      {(create.error || accept.error) && (
        <p role="alert" className="text-theme-sm text-error-500">
          {create.error ?? accept.error}
        </p>
      )}
    </div>
  );
}
