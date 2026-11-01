import { describe, expect, it } from 'vitest';

import { kindLine, listQuery, orgHintLine, pushedLabel, syncRetryAt, syncedAgo } from './index';

describe('syncedAgo', () => {
  const now = new Date('2026-11-01T12:00:00Z');
  it('says when there was no sync yet', () => {
    expect(syncedAgo(null, now)).toBe('not synced yet');
    expect(syncedAgo('nonsense', now)).toBe('not synced yet');
  });
  it('rounds to minutes, hours, then days', () => {
    expect(syncedAgo('2026-11-01T11:59:30Z', now)).toBe('synced just now');
    expect(syncedAgo('2026-11-01T11:35:00Z', now)).toBe('synced 25 min ago');
    expect(syncedAgo('2026-11-01T09:10:00Z', now)).toBe('synced 3 h ago');
    expect(syncedAgo('2026-10-29T12:00:00Z', now)).toBe('synced 3 days ago');
  });
});

describe('pushedLabel and kindLine', () => {
  it('shows the day of the last push, the year when it is another one', () => {
    expect(pushedLabel('2026-10-05T08:00:00Z', '2026-11-01')).toBe('Oct 5');
    expect(pushedLabel('2024-02-10T08:00:00Z', '2026-11-01')).toBe('Feb 10, 2024');
    expect(pushedLabel(null, '2026-11-01')).toBe('never pushed');
  });
  it('names what the repository is', () => {
    expect(kindLine({ visibility: 'public', fork: false, archived: false })).toBe('public');
    expect(kindLine({ visibility: 'private', fork: true, archived: true })).toBe(
      'archived private fork',
    );
  });
});

describe('orgHintLine', () => {
  it('names the organisation to approve the app in', () => {
    expect(orgHintLine(null)).toBeNull();
    expect(orgHintLine('cybercapybara')).toMatch(/^cybercapybara may restrict/);
  });
});

describe('syncRetryAt', () => {
  const now = new Date('2026-11-01T12:00:00Z');
  it('takes retry_after_sec from the 429 body, a minute without it', () => {
    expect(syncRetryAt({ retry_after_sec: 42 }, now)).toBe(now.getTime() + 42_000);
    expect(syncRetryAt({ error: 'sync_too_soon' }, now)).toBe(now.getTime() + 60_000);
    expect(syncRetryAt(undefined, now)).toBe(now.getTime() + 60_000);
  });
});

describe('listQuery', () => {
  it('keeps only the filters that are set', () => {
    expect(listQuery({ view: 'attention' })).toEqual({ view: 'attention' });
    expect(listQuery({ view: 'all', owner: 'moveeeax', q: '' })).toEqual({
      view: 'all',
      owner: 'moveeeax',
    });
    expect(listQuery({ view: 'archived', q: 'terraform' })).toEqual({
      view: 'archived',
      q: 'terraform',
    });
  });
});
