import { describe, expect, it } from 'vitest';

import { carriesGive, GIVE_TYPE, readGive } from './drag';

const dt = (data: Record<string, string>) =>
  ({ types: Object.keys(data), getData: (t: string) => data[t] ?? '' }) as unknown as DataTransfer;

describe('readGive', () => {
  it('takes the four kinds of site rows', () => {
    for (const kind of ['task', 'note', 'event'])
      expect(readGive(dt({ [GIVE_TYPE]: JSON.stringify({ kind, id: 'x', title: 'T' }) }))).toEqual({
        kind,
        id: 'x',
        title: 'T',
      });
    const tx = { kind: 'transaction', id: 'x', name: 'Кофе', merchant: null };
    expect(readGive(dt({ [GIVE_TYPE]: JSON.stringify(tx) }))).toEqual(tx);
  });

  it('lets go of text, links, files, unknown kinds and broken data', () => {
    expect(readGive(dt({ 'text/plain': 'Анна любит пионы' }))).toBeNull();
    expect(readGive(dt({ 'text/uri-list': 'https://example.com', 'text/plain': 'x' }))).toBeNull();
    expect(readGive(dt({ Files: '' }))).toBeNull();
    expect(readGive(dt({ [GIVE_TYPE]: JSON.stringify({ kind: 'habit', id: 'h' }) }))).toBeNull();
    expect(readGive(dt({ [GIVE_TYPE]: JSON.stringify({ kind: 'task' }) }))).toBeNull();
    expect(readGive(dt({ [GIVE_TYPE]: '{' }))).toBeNull();
  });
});

describe('carriesGive', () => {
  it('is true only for a drag with a site row', () => {
    expect(carriesGive(dt({ [GIVE_TYPE]: '{}', 'text/plain': 'T' }))).toBe(true);
    expect(carriesGive(dt({ 'text/plain': 'T' }))).toBe(false);
    expect(carriesGive(null)).toBe(false);
  });
});
