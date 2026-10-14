import { useEffect, useRef, useState } from 'react';
import { Link, useBlocker, useNavigate, useParams } from 'react-router';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { useDeleteNote, useNote, useSaveNote } from '@/hooks/useNotes';
import { useProjects } from '@/hooks/useProjects';
import { draftOf, isDirty, mergeSaved, tagsFrom, type Draft, type Note } from '@/lib/notes';

import { LoadError, NotesFrame, Placeholder } from './frame';
import { NoteView } from './NoteView';

/** One note: the fields of a blog post, the Markdown source and its preview side by side. */
export function NoteEditorPage() {
  const { id = '' } = useParams();
  const note = useNote(id);
  if (note.isPending) {
    return (
      <NotesFrame headline="Note">
        <Placeholder />
      </NotesFrame>
    );
  }
  if (note.isError || !note.data) {
    return (
      <NotesFrame headline="Note">
        <LoadError error={note.error} onRetry={() => void note.refetch()} />
      </NotesFrame>
    );
  }
  return <Editor key={note.data.id} note={note.data} />;
}

function Editor({ note }: { note: Note }) {
  const navigate = useNavigate();
  const [d, setD] = useState<Draft>(() => draftOf(note));
  const [mode, setMode] = useState<'write' | 'preview'>('write');
  const [confirm, setConfirm] = useState(false);
  const projects = (useProjects().data ?? []).filter(
    (p) => p.status !== 'done' || p.id === note.project_id,
  );
  // The last version the server confirmed: what "unsaved" compares against.
  const [base, setBase] = useState<Draft>(() => draftOf(note));
  // What the save in flight sent, so words typed meanwhile survive its answer.
  const sent = useRef<Draft | null>(null);
  const save = useSaveNote(note.id, (n) => {
    const saved = draftOf(n);
    const submitted = sent.current;
    setBase(saved);
    setD((cur) => (submitted ? mergeSaved(cur, submitted, saved) : saved));
  });
  // Deleting leaves on purpose; the blocker must not ask about the draft.
  const leaving = useRef(false);
  const remove = useDeleteNote(() => {
    leaving.current = true;
    navigate('/notes');
  });
  const dirty = isDirty(d, base);
  const set = (k: keyof Draft) => (e: { target: { value: string } }) =>
    setD((cur) => ({ ...cur, [k]: e.target.value }));

  // Leaving the page with unsaved words asks first: the tab closing through
  // the browser's prompt, a move inside the app through the kit confirm.
  useEffect(() => {
    if (!dirty) return;
    const warn = (e: BeforeUnloadEvent) => e.preventDefault();
    window.addEventListener('beforeunload', warn);
    return () => window.removeEventListener('beforeunload', warn);
  }, [dirty]);
  const blocker = useBlocker(
    ({ currentLocation, nextLocation }) =>
      dirty && !leaving.current && currentLocation.pathname !== nextLocation.pathname,
  );

  const submit = () => {
    sent.current = d;
    save.mutate({
      title: d.title.trim(),
      topic: d.topic.trim(),
      tags: tagsFrom(d.tags),
      summary: d.summary.trim(),
      body: d.body,
      project_id: d.project || null,
      ...(d.slug.trim() && d.slug.trim() !== base.slug ? { slug: d.slug.trim() } : {}),
    });
  };

  return (
    <NotesFrame
      headline={<span data-num-ok>{d.title || 'Untitled'}</span>}
      sub={
        <>
          Private
          {note.project_title && note.project_id && (
            <>
              {' · '}
              <Link
                className="k-link"
                to={
                  note.project_kind === 'course'
                    ? `/learning/${note.project_id}`
                    : `/tasks/projects/${note.project_id}`
                }
                data-num-ok
              >
                {note.project_title}
              </Link>
            </>
          )}
          {dirty && ' · unsaved'}
        </>
      }
      tools={
        <>
          <div className="k-seg n-mode" role="group" aria-label="Editor">
            <button
              type="button"
              aria-pressed={mode === 'write'}
              className={mode === 'write' ? 'k-seg-on' : undefined}
              onClick={() => setMode('write')}
            >
              Write
            </button>
            <button
              type="button"
              aria-pressed={mode === 'preview'}
              className={mode === 'preview' ? 'k-seg-on' : undefined}
              onClick={() => setMode('preview')}
            >
              Preview
            </button>
          </div>
          <span className="k-grow" />
          <button type="button" className="k-btn k-danger" onClick={() => setConfirm(true)}>
            Delete
          </button>
          <button
            type="button"
            className="k-btn k-solid"
            disabled={!dirty || save.isPending}
            onClick={submit}
          >
            {save.isPending ? 'Saving…' : 'Save'}
          </button>
        </>
      }
    >
      <div className="k-form">
        <label className="k-field">
          <span>Title</span>
          <input required maxLength={200} value={d.title} onChange={set('title')} />
        </label>
        <div className="k-fields">
          <label className="k-field">
            <span>Topic</span>
            <input maxLength={60} value={d.topic} onChange={set('topic')} />
          </label>
          <label className="k-field">
            <span>Tags, comma between</span>
            <input value={d.tags} onChange={set('tags')} />
          </label>
        </div>
        <div className="k-fields">
          <label className="k-field">
            <span>Project or course</span>
            <select value={d.project} onChange={set('project')}>
              <option value="">None</option>
              {projects.map((p) => (
                <option key={p.id} value={p.id}>
                  {p.kind === 'course' ? `${p.name} (course)` : p.name}
                </option>
              ))}
            </select>
          </label>
          <label className="k-field">
            <span>Slug</span>
            <input
              maxLength={120}
              pattern="[a-z0-9]+(-[a-z0-9]+)*"
              value={d.slug}
              onChange={set('slug')}
            />
          </label>
        </div>
        <label className="k-field">
          <span>Summary</span>
          <input
            maxLength={500}
            value={d.summary}
            onChange={set('summary')}
            placeholder="One or two lines for a list"
          />
        </label>
      </div>
      {save.error && (
        <p role="alert" className="k-error">
          {save.error}
        </p>
      )}
      <div className={`n-editor n-show-${mode}`}>
        <textarea
          className="n-source"
          aria-label="Markdown"
          value={d.body}
          onChange={set('body')}
          spellCheck
          placeholder={
            '# Heading\n\nWrite in Markdown: **bold**, *italic*, `code`, lists, > quotes, ``` code blocks.'
          }
        />
        <div className="n-preview" aria-label="Preview">
          <NoteView body={d.body} />
        </div>
      </div>
      {confirm && (
        <ConfirmDialog
          title={`Delete “${note.title}”?`}
          description="The note goes for good."
          confirmLabel="Delete"
          destructive
          busy={remove.isPending}
          onConfirm={() => remove.mutate(note.id)}
          onClose={() => setConfirm(false)}
        />
      )}
      {blocker.state === 'blocked' && (
        <ConfirmDialog
          title="Leave without saving?"
          description="The changes to this note are not saved yet."
          confirmLabel="Leave"
          destructive
          onConfirm={() => blocker.proceed()}
          onClose={() => blocker.reset()}
        />
      )}
    </NotesFrame>
  );
}
