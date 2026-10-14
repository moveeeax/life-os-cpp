import { describe, expect, it } from 'vitest';

import { PROJECT_STATUS, courseProgress, takeProjectCode } from './projects';

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
