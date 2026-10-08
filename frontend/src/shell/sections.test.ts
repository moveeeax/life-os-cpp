import { describe, expect, it } from 'vitest';

import { Permission, type PermissionUser } from '@/lib/auth/permissions';

import { rootTarget, sections, visibleSections } from './sections';

const user = (permissions: number) =>
  ({ role: { id: 1, name: 'role', permissions } }) as PermissionUser;

describe('visibleSections', () => {
  it('lists Health first and keeps every not-yet-built section as an inactive item', () => {
    const list = visibleSections(user(Permission.General | Permission.FitnessRead));
    expect(list[0]).toMatchObject({ key: 'health', path: '/health' });
    expect(list.find((s) => s.key === 'workout')).toMatchObject({ path: '/workout' });
    expect(list.find((s) => s.key === 'food')).toMatchObject({ path: '/food' });
    expect(list.find((s) => s.key === 'money')).toMatchObject({ path: '/money' });
    expect(list.find((s) => s.key === 'tasks')).toMatchObject({ path: '/tasks' });
    expect(list.find((s) => s.key === 'goals')).toMatchObject({ path: '/goals' });
    expect(list.filter((s) => !s.path).map((s) => s.label)).toEqual([
      'Day',
      'Journal',
      'Learning',
      'Travel',
      'Work',
      'Freelance',
      'Ideas',
      'OSS',
    ]);
  });

  it('shows a section whose module is off as not built yet, in its place', () => {
    const who = user(Permission.General | Permission.FitnessRead);
    const list = visibleSections(who, new Set(['workout']));
    expect(list).toHaveLength(sections.length);
    expect(list.find((s) => s.key === 'workout')?.path).toBeUndefined();
    expect(list.map((s) => s.key)).toEqual(sections.map((s) => s.key));
    // Without the permission the inactive item still shows, like the other unbuilt ones.
    expect(
      visibleSections(user(Permission.General), new Set(['workout'])).some(
        (s) => s.key === 'workout',
      ),
    ).toBe(true);
  });

  it('hides Health from a user without fitness:read but keeps the inactive items', () => {
    const list = visibleSections(user(Permission.General));
    expect(list.find((s) => s.key === 'health')).toBeUndefined();
    expect(list.find((s) => s.key === 'workout')).toBeUndefined();
    expect(list).toHaveLength(sections.length - 2);
  });

  it('shows Food to any user and as not built yet while its module is off', () => {
    expect(visibleSections(user(Permission.General)).find((s) => s.key === 'food')).toMatchObject({
      path: '/food',
    });
    expect(
      visibleSections(user(Permission.General), new Set(['food'])).find((s) => s.key === 'food')
        ?.path,
    ).toBeUndefined();
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
