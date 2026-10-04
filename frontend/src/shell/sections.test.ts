import { describe, expect, it } from 'vitest';

import { Permission, type PermissionUser } from '@/lib/auth/permissions';

import { rootTarget, sections, visibleSections } from './sections';

const user = (permissions: number) =>
  ({ role: { id: 1, name: 'role', permissions } }) as PermissionUser;

describe('visibleSections', () => {
  it('lists Health first and keeps every not-yet-built section as an inactive item', () => {
    const list = visibleSections(user(Permission.General | Permission.FitnessRead));
    expect(list[0]).toMatchObject({ key: 'health', path: '/health' });
    expect(list.filter((s) => !s.path).map((s) => s.label)).toEqual([
      'Day',
      'Money',
      'Tasks',
      'Goals',
      'Journal',
      'Learning',
      'Workout',
      'Travel',
      'Work',
      'Freelance',
      'Ideas',
      'OSS',
    ]);
  });

  it('hides Health from a user without fitness:read but keeps the inactive items', () => {
    const list = visibleSections(user(Permission.General));
    expect(list.find((s) => s.key === 'health')).toBeUndefined();
    expect(list).toHaveLength(sections.length - 1);
  });

  it('shows Health to an administrator through the sentinel bit', () => {
    expect(visibleSections(user(Permission.Administer))[0].key).toBe('health');
  });

  it('returns only inactive items for no user', () => {
    expect(visibleSections(null).every((s) => !s.path)).toBe(true);
  });
});

describe('rootTarget', () => {
  it('waits while the session is loading', () => {
    expect(rootTarget({ isPending: true, data: undefined })).toBe('loading');
  });
  it('shows the sign-in form to a guest', () => {
    expect(rootTarget({ isPending: false, data: null })).toBe('signin');
  });
  it('shows the sign-in form when the session request failed', () => {
    expect(rootTarget({ isPending: false, data: undefined })).toBe('signin');
  });
  it('sends a signed-in user to the dashboard', () => {
    expect(rootTarget({ isPending: false, data: { role: { permissions: 1 } } })).toBe('dashboard');
  });
});
