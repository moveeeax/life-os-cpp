import { useState, type FormEvent } from 'react';

import { useSaveProject } from '@/hooks/useProjects';
import type { Project, ProjectInput } from '@/lib/tasks';
import { Sheet } from '@/shell/kit';

/** Create or edit a project, or a course (kind "course": platform, dates, certificate). */
export function ProjectSheet({
  project,
  kind = project?.kind ?? 'project',
  onClose,
  onSaved,
}: {
  project?: Project;
  kind?: Project['kind'];
  onClose: () => void;
  onSaved?: (p: Project) => void;
}) {
  const course = kind === 'course';
  const [f, setF] = useState({
    name: project?.name ?? '',
    code: project?.code ?? '',
    description: project?.description ?? '',
    link: project?.link ?? '',
    status: project?.status ?? (course ? 'planned' : 'active'),
    platform: project?.platform ?? '',
    starts_on: project?.starts_on ?? '',
    ends_on: project?.ends_on ?? '',
    certificate: project?.certificate ?? '',
  });
  const save = useSaveProject(project?.id ?? null, (p) => {
    onSaved?.(p);
    onClose();
  });
  const set = (k: keyof typeof f) => (e: { target: { value: string } }) =>
    setF({ ...f, [k]: e.target.value });
  const submit = (e: FormEvent) => {
    e.preventDefault();
    const body: ProjectInput = {
      name: f.name.trim(),
      description: f.description.trim(),
      link: f.link.trim(),
      status: f.status as ProjectInput['status'],
      ...(f.code.trim() ? { code: f.code.trim() } : {}),
      ...(project ? {} : { kind }),
      ...(course
        ? {
            platform: f.platform.trim(),
            starts_on: f.starts_on || null,
            ends_on: f.ends_on || null,
            certificate: f.certificate.trim(),
          }
        : {}),
    };
    save.mutate(body);
  };
  const noun = course ? 'course' : 'project';
  return (
    <Sheet title={project ? `Edit the ${noun}` : `New ${noun}`} onClose={onClose} width={460}>
      <form className="k-form" onSubmit={submit}>
        <label className="k-field">
          <span>Name</span>
          <input
            required
            maxLength={120}
            placeholder={course ? 'Kubernetes CKS' : 'CyberCapybara'}
            value={f.name}
            onChange={set('name')}
          />
        </label>
        <div className="k-fields">
          <label className="k-field">
            <span>Code, for #code in a task</span>
            <input
              maxLength={24}
              pattern="[a-z0-9-]{1,24}"
              placeholder="made from the name"
              value={f.code}
              onChange={set('code')}
            />
          </label>
          <label className="k-field">
            <span>Status</span>
            <select value={f.status} onChange={set('status')}>
              {(course ? ['planned', 'active', 'done'] : ['active', 'paused', 'done']).map((s) => (
                <option key={s} value={s}>
                  {s[0].toUpperCase() + s.slice(1)}
                </option>
              ))}
            </select>
          </label>
        </div>
        {course && (
          <>
            <label className="k-field">
              <span>Platform</span>
              <input
                maxLength={80}
                placeholder="Linux Foundation"
                value={f.platform}
                onChange={set('platform')}
              />
            </label>
            <div className="k-fields">
              <label className="k-field">
                <span>Starts</span>
                <input type="date" value={f.starts_on} onChange={set('starts_on')} />
              </label>
              <label className="k-field">
                <span>Ends</span>
                <input type="date" value={f.ends_on} onChange={set('ends_on')} />
              </label>
            </div>
          </>
        )}
        <label className="k-field">
          <span>What it is</span>
          <input maxLength={500} value={f.description} onChange={set('description')} />
        </label>
        <label className="k-field">
          <span>Link</span>
          <input type="url" maxLength={500} value={f.link} onChange={set('link')} />
        </label>
        {course && (
          <label className="k-field">
            <span>Certificate link</span>
            <input type="url" maxLength={500} value={f.certificate} onChange={set('certificate')} />
          </label>
        )}
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
            {save.isPending ? 'Saving…' : project ? 'Save' : 'Add'}
          </button>
        </div>
      </form>
    </Sheet>
  );
}
