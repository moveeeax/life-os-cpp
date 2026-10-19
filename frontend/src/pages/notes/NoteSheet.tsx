import { useState, type FormEvent } from 'react';
import { useNavigate } from 'react-router';

import { useSaveNote } from '@/hooks/useNotes';
import { useProjects } from '@/hooks/useProjects';
import { tagsFrom } from '@/lib/notes';
import { Sheet } from '@/shell/kit';

/** A new note: title, topic, tags, its project or course and the day it is about; it opens in the editor. */
export function NoteSheet({
  projectId,
  taskId,
  title: initialTitle = '',
  onClose,
}: {
  projectId?: string | null;
  taskId?: string | null;
  title?: string;
  onClose: () => void;
}) {
  const navigate = useNavigate();
  const projects = (useProjects().data ?? []).filter(
    (p) => p.status !== 'done' || p.id === projectId,
  );
  const [f, setF] = useState({
    title: initialTitle,
    topic: '',
    tags: '',
    project: projectId ?? '',
    date: '',
  });
  const save = useSaveNote(null, (n) => {
    onClose();
    navigate(`/notes/${n.id}`);
  });
  const set = (k: keyof typeof f) => (e: { target: { value: string } }) =>
    setF({ ...f, [k]: e.target.value });
  const submit = (e: FormEvent) => {
    e.preventDefault();
    save.mutate({
      title: f.title.trim(),
      topic: f.topic.trim(),
      tags: tagsFrom(f.tags),
      project_id: f.project || null,
      ...(f.date ? { date: f.date } : {}),
      ...(taskId ? { task_id: taskId } : {}),
    });
  };
  return (
    <Sheet title="New note" onClose={onClose} width={460}>
      <form className="k-form" onSubmit={submit}>
        <label className="k-field">
          <span>Title</span>
          <input
            required
            maxLength={200}
            value={f.title}
            onChange={set('title')}
            placeholder="What it is about"
          />
        </label>
        <div className="k-fields">
          <label className="k-field">
            <span>Topic</span>
            <input
              maxLength={60}
              value={f.topic}
              onChange={set('topic')}
              placeholder="Kubernetes"
            />
          </label>
          <label className="k-field">
            <span>Tags, comma between</span>
            <input value={f.tags} onChange={set('tags')} placeholder="otel, tracing" />
          </label>
        </div>
        <div className="k-fields">
          <label className="k-field">
            <span>Project or course</span>
            <select value={f.project} onChange={set('project')}>
              <option value="">None</option>
              {projects.map((p) => (
                <option key={p.id} value={p.id}>
                  {p.kind === 'course' ? `${p.name} (course)` : p.name}
                </option>
              ))}
            </select>
          </label>
          <label className="k-field">
            <span>Day it is about (optional)</span>
            <input type="date" value={f.date} onChange={set('date')} />
          </label>
        </div>
        {save.error && (
          <p role="alert" className="k-error">
            {save.error}
          </p>
        )}
        <div className="k-dlg-actions">
          <button type="button" className="k-btn" onClick={onClose}>
            Cancel
          </button>
          <button type="submit" className="k-btn k-solid" disabled={save.isPending}>
            {save.isPending ? 'Creating…' : 'Create and write'}
          </button>
        </div>
      </form>
    </Sheet>
  );
}
