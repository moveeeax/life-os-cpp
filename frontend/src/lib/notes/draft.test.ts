import { describe, expect, it } from 'vitest';

import { draftOf, isDirty, mergeSaved, type Draft } from './draft';
import type { Note } from './types';

const note = (over: Partial<Note> = {}): Note =>
  ({
    id: 'n1',
    slug: 'state',
    title: 'State',
    summary: '',
    body: 'one',
    status: 'private',
    topic: 'Terraform',
    tags: ['terraform', 'state'],
    project_id: null,
    task_id: null,
    published_at: null,
    created_at: '2026-10-01T00:00:00Z',
    updated_at: '2026-10-01T00:00:00Z',
    ...over,
  }) as Note;

describe('draftOf', () => {
  it('turns a note into editable strings', () => {
    expect(draftOf(note({ project_id: 'p1' }))).toEqual({
      title: 'State',
      topic: 'Terraform',
      tags: 'terraform, state',
      summary: '',
      slug: 'state',
      project: 'p1',
      body: 'one',
    });
  });

  it('a missing body or project is an empty string', () => {
    const d = draftOf(note({ body: undefined, project_id: null }));
    expect(d.body).toBe('');
    expect(d.project).toBe('');
  });
});

describe('isDirty', () => {
  it('compares every field', () => {
    const base = draftOf(note());
    expect(isDirty(base, base)).toBe(false);
    expect(isDirty({ ...base, summary: 'x' }, base)).toBe(true);
  });
});

describe('mergeSaved', () => {
  const submitted: Draft = { ...draftOf(note()), title: ' State ', body: 'two' };

  it('takes the saved note when nothing changed during the save', () => {
    const saved = draftOf(note({ title: 'State', body: 'two' }));
    expect(mergeSaved(submitted, submitted, saved)).toEqual(saved);
  });

  it('keeps what was typed while the save was in flight', () => {
    const saved = draftOf(note({ title: 'State', body: 'two' }));
    const current = { ...submitted, body: 'two and three' };
    const merged = mergeSaved(current, submitted, saved);
    expect(merged.body).toBe('two and three');
    // The untouched title takes the server's trimmed form.
    expect(merged.title).toBe('State');
    expect(isDirty(merged, saved)).toBe(true);
  });

  it('each field is merged on its own', () => {
    const saved = draftOf(note({ title: 'State', body: 'two', tags: ['a'] }));
    const current = { ...submitted, tags: 'a, b' };
    const merged = mergeSaved(current, submitted, saved);
    expect(merged.tags).toBe('a, b');
    expect(merged.body).toBe('two');
  });
});
