/** The bodies of the task edit form and the typed capture (pinned by bodies.test.ts). */
import type { Task, TaskPatch } from './index';
import { sameRepeat, type TaskRepeat } from './repeat';

export interface TaskDraft {
  title: string;
  area: string;
  effort: string;
  due: string;
  /** HH:MM on the due day, empty for none; left out, the task keeps its own. */
  dueTime?: string;
  next: string;
  note: string;
  status: string;
  goalId: string;
  sectionId: string;
  /** The project or course; left out, the task keeps its own. */
  projectId?: string;
  /** The repeat rule, null for none; left out, the task keeps its own. */
  repeat?: TaskRepeat | null;
}

type TaskFields = Pick<
  Task,
  | 'title'
  | 'area'
  | 'effort'
  | 'due'
  | 'next_step'
  | 'note'
  | 'status'
  | 'goal_id'
  | 'goal_section_id'
> & { project_id?: string | null; repeat?: TaskRepeat | null; due_time?: string | null };

/** Only the fields that changed; a section travels with its goal (the API checks the pair). */
export function taskPatch(task: TaskFields, d: TaskDraft): TaskPatch {
  const patch: TaskPatch = {};
  if (d.title.trim() !== task.title) patch.title = d.title.trim();
  if (d.area !== task.area) patch.area = d.area as TaskPatch['area'];
  if ((d.effort || null) !== task.effort) patch.effort = (d.effort || null) as TaskPatch['effort'];
  if ((d.due || null) !== task.due) patch.due = d.due || null;
  if (d.dueTime !== undefined && d.due && (d.dueTime || null) !== (task.due_time ?? null)) {
    patch.due_time = d.dueTime || null;
  }
  if (d.next.trim() !== task.next_step) patch.next_step = d.next.trim();
  if (d.note !== task.note) patch.note = d.note;
  if (d.status !== task.status) patch.status = d.status as TaskPatch['status'];
  if ((d.goalId || null) !== task.goal_id || (d.sectionId || null) !== task.goal_section_id) {
    patch.goal_id = d.goalId || null;
    patch.goal_section_id = d.goalId && d.sectionId ? d.sectionId : null;
  }
  if (d.projectId !== undefined && (d.projectId || null) !== (task.project_id ?? null)) {
    patch.project_id = d.projectId || null;
  }
  if (d.repeat !== undefined && !sameRepeat(d.repeat, task.repeat)) {
    patch.repeat = d.repeat;
  }
  return patch;
}

/** A phrase typed into the capture without the parse: a project task, cut to 200 characters. */
export function captureBody(text: string, defaultDue?: string) {
  return {
    title: text.trim().slice(0, 200),
    area: 'projects' as const,
    ...(defaultDue ? { due: defaultDue } : {}),
  };
}
