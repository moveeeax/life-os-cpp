import { describe, expect, it } from 'vitest';

import {
  PROJECT_STATUS,
  TASK_PAGE,
  courseProgress,
  deadlineText,
  loadProjectTasks,
  takeProjectCode,
} from './projects';
import type { Task } from './types';

const projects = [
  { id: 'p1', code: 'small' },
  { id: 'p2', code: 'life-os' },
];

describe('takeProjectCode', () => {
  it('binds the phrase to the project its #code names and takes the code out', () => {
    expect(takeProjectCode('write the runbook #small tomorrow', projects)).toEqual({
      text: 'write the runbook tomorrow',
      projectId: 'p1',
    });
  });
  it('matches the code in any case, at the start or the end', () => {
    expect(takeProjectCode('#Life-OS habits spec', projects).projectId).toBe('p2');
    expect(takeProjectCode('habits spec #life-os', projects)).toEqual({
      text: 'habits spec',
      projectId: 'p2',
    });
  });
  it('leaves an unknown #tag in the text', () => {
    expect(takeProjectCode('fix #42 in the parser', projects)).toEqual({
      text: 'fix #42 in the parser',
      projectId: null,
    });
  });
  it('keeps a phrase that is only a code as the text, with no project', () => {
    expect(takeProjectCode('#small', projects)).toEqual({ text: '#small', projectId: null });
  });
});

describe('courseProgress', () => {
  it('counts done steps of all, none without steps', () => {
    expect(courseProgress({ open: 8, done: 6 })).toEqual({ done: 6, total: 14, share: 6 / 14 });
    expect(courseProgress({ open: 0, done: 0 })).toBeNull();
  });
});

describe('PROJECT_STATUS', () => {
  it('names every status', () => {
    expect(Object.keys(PROJECT_STATUS).sort()).toEqual(['active', 'done', 'paused', 'planned']);
  });
});

describe('loadProjectTasks', () => {
  const task = (i: number, status: Task['status']) =>
    ({
      id: `${status}-${i}`,
      status,
      created_at: new Date(Date.UTC(2026, 0, 1) + i * 60_000).toISOString(),
    }) as Task;

  it('pages through every open task and keeps one page of done ones', async () => {
    const open = Array.from({ length: 450 }, (_, i) => task(i, 'open'));
    const going = [task(1000, 'in_progress')];
    const done = Array.from({ length: 300 }, (_, i) => task(2000 + i, 'done'));
    const by: Record<string, Task[]> = { open, in_progress: going, done };
    const calls: string[] = [];
    const out = await loadProjectTasks(async (status, limit, offset) => {
      calls.push(`${status}:${offset}`);
      const rows = by[status];
      return { data: rows.slice(offset, offset + limit), total: rows.length };
    });
    expect(out.filter((t) => t.status === 'open')).toHaveLength(450);
    expect(out.filter((t) => t.status === 'in_progress')).toHaveLength(1);
    expect(out.filter((t) => t.status === 'done')).toHaveLength(TASK_PAGE);
    expect(calls.sort()).toEqual(['done:0', 'in_progress:0', 'open:0', 'open:200', 'open:400']);
    // Newest first across the statuses.
    expect(out[0].created_at >= out[out.length - 1].created_at).toBe(true);
    expect(out[0].status).toBe('done');
  });
});

describe('deadlineText', () => {
  const today = '2026-10-19';
  it('says how far the deadline is, and nothing once the project is done', () => {
    expect(deadlineText({ deadline: null, status: 'active' }, today)).toBeNull();
    expect(deadlineText({ deadline: '2026-10-01', status: 'done' }, today)).toBeNull();
    expect(deadlineText({ deadline: '2026-10-19', status: 'active' }, today)).toEqual({
      text: 'due today',
      late: false,
    });
    expect(deadlineText({ deadline: '2026-10-16', status: 'paused' }, today)).toEqual({
      text: '3 days late',
      late: true,
    });
    expect(deadlineText({ deadline: '2026-12-31', status: 'active' }, today)?.text).toBe(
      'by 31 Dec',
    );
    expect(deadlineText({ deadline: '2027-03-31', status: 'active' }, today)?.text).toBe(
      'by 31 Mar 2027',
    );
  });
});
