import { useState, type FormEvent } from 'react';

import { Modal } from '@/components/Modal';
import { useCreateNote, useUpdateNote } from '@/hooks/useTasks';
import type { TaskNote } from '@/lib/tasks';

import { modalPanel } from '../workout/styles';
import { Capture } from './Capture';
import { useTasksDay } from './counts';
import { LoadError, Placeholder, Section, TasksFrame } from './frame';

/** Thoughts and links that are not tasks yet; each becomes a task or goes to the archive. */
export function TasksInboxPage() {
  const { notes, counts } = useTasksDay();
  const [text, setText] = useState('');
  const [turning, setTurning] = useState<TaskNote | null>(null);
  const add = useCreateNote(() => setText(''));
  const update = useUpdateNote();

  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (text.trim()) add.mutate(text.trim());
  };

  return (
    <TasksFrame counts={counts}>
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
          maxLength={1000}
          placeholder="A thought or a link for later"
          aria-label="New note"
          autoComplete="off"
          className="min-w-0 flex-1 bg-transparent py-1.5 text-theme-sm text-gray-800 outline-none placeholder:text-gray-400 dark:text-white/90"
        />
      </form>
      {notes.isPending ? (
        <Placeholder className="h-32" />
      ) : notes.isError ? (
        <LoadError error={notes.error} onRetry={() => void notes.refetch()} />
      ) : notes.data.length === 0 ? (
        <p className="text-theme-sm text-gray-400">Inbox is empty.</p>
      ) : (
        <Section title="Not tasks yet">
          <ul className="divide-y divide-gray-100 dark:divide-white/5">
            {notes.data.map((n) => (
              <li
                key={n.id}
                className="grid grid-cols-[minmax(0,1fr)_auto] items-center gap-3.5 py-2.5"
              >
                <span className="text-theme-sm break-words text-gray-800 dark:text-white/90">
                  {n.text}
                </span>
                <span className="flex gap-3.5 text-theme-sm">
                  <button
                    type="button"
                    onClick={() => setTurning(n)}
                    className="font-medium text-brand-500"
                  >
                    Task
                  </button>
                  <button
                    type="button"
                    disabled={update.isPending}
                    onClick={() => update.mutate({ id: n.id, status: 'archived' })}
                    className="text-gray-500"
                  >
                    Archive
                  </button>
                </span>
              </li>
            ))}
          </ul>
        </Section>
      )}
      {(add.error || update.error) && (
        <p role="alert" className="text-theme-sm text-error-500">
          {add.error ?? update.error}
        </p>
      )}
      {turning && (
        <Modal onClose={() => setTurning(null)} className="max-w-lg">
          <div className={modalPanel}>
            <h2 className="mb-4 text-lg font-semibold">Make it a task</h2>
            <Capture
              initial={turning.text}
              noteId={turning.id}
              autoStart
              onDone={() => setTurning(null)}
            />
          </div>
        </Modal>
      )}
    </TasksFrame>
  );
}
