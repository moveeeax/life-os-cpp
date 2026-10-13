import { describe, expect, it } from 'vitest';

import { captureBody, taskPatch } from './bodies';

const task = {
  title: 'Pay rent',
  area: 'finance',
  effort: null,
  due: '2026-10-20',
  next_step: '',
  note: '',
  status: 'open',
  goal_id: null,
  goal_section_id: null,
} as const;

describe('taskPatch', () => {
  it('sends only what changed', () => {
    expect(
      taskPatch(task, {
        title: ' Pay rent ',
        area: 'finance',
        effort: '',
        due: '2026-10-21',
        next: '',
        note: '',
        status: 'open',
        goalId: '',
        sectionId: '',
      }),
    ).toEqual({ due: '2026-10-21' });
  });
  it('a section travels with its goal', () => {
    expect(
      taskPatch(task, {
        title: 'Pay rent',
        area: 'finance',
        effort: '',
        due: '2026-10-20',
        next: '',
        note: '',
        status: 'open',
        goalId: 'g1',
        sectionId: 's1',
      }),
    ).toEqual({ goal_id: 'g1', goal_section_id: 's1' });
    expect(
      taskPatch(
        { ...task, goal_id: 'g1', goal_section_id: 's1' },
        {
          title: 'Pay rent',
          area: 'finance',
          effort: '',
          due: '2026-10-20',
          next: '',
          note: '',
          status: 'open',
          goalId: '',
          sectionId: 's1',
        },
      ),
    ).toEqual({ goal_id: null, goal_section_id: null });
  });
});

describe('captureBody', () => {
  it('a typed phrase becomes a project task, cut to 200, with the default due', () => {
    expect(captureBody('  Buy milk ', '2026-10-13')).toEqual({
      title: 'Buy milk',
      area: 'projects',
      due: '2026-10-13',
    });
    expect(captureBody('x'.repeat(250)).title).toHaveLength(200);
    expect(captureBody('Buy milk')).not.toHaveProperty('due');
  });
});
