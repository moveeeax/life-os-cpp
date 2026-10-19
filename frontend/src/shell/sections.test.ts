import { describe, expect, it } from 'vitest';

import { Permission, type PermissionUser } from '@/lib/auth/permissions';

import { DASHBOARD_HOME, glassNav, homeOf, rootTarget } from './sections';

const user = (permissions: number) =>
  ({ role: { id: 1, name: 'role', permissions } }) as PermissionUser;

describe('glassNav', () => {
  it('lists the sections of the glass nav in order', () => {
    const list = glassNav(user(Permission.General | Permission.FitnessRead));
    expect(list.map((s) => [s.label, s.to])).toEqual([
      ['Now', '/now'],
      ['Tasks', '/tasks'],
      ['Goals', '/goals'],
      ['Habits', '/habits'],
      ['Learning', '/learning'],
      ['Notes', '/notes'],
      ['Practice', '/practice'],
      ['Travel', '/travel'],
      ['Money', '/money'],
      ['Food', '/food'],
      ['Body', '/health'],
      ['Training', '/workout'],
    ]);
  });

  it('hides Body and Training without fitness:read', () => {
    expect(glassNav(user(Permission.General)).map((s) => s.label)).toEqual([
      'Now',
      'Tasks',
      'Goals',
      'Habits',
      'Learning',
      'Notes',
      'Practice',
      'Travel',
      'Money',
      'Food',
    ]);
  });

  it('shows Body at the medical records without fitness:read while medical is on', () => {
    const list = glassNav(user(Permission.General), new Set(), new Set(['medical']));
    expect(list.filter((s) => s.key === 'health' || s.key === 'workout')).toEqual([
      expect.objectContaining({ label: 'Body', to: '/health/records' }),
    ]);
  });

  it('keeps Body at the band page with fitness:read while medical is on', () => {
    const list = glassNav(
      user(Permission.General | Permission.FitnessRead),
      new Set(),
      new Set(['medical']),
    );
    expect(list.find((s) => s.key === 'health')?.to).toBe('/health');
  });

  it('shows Body and Training to an administrator through the sentinel bit', () => {
    expect(glassNav(user(Permission.Administer)).map((s) => s.label)).toContain('Training');
  });

  it('leaves out a section whose module is off', () => {
    const list = glassNav(user(Permission.General), new Set(['goals']));
    expect(list.map((s) => s.label)).toEqual([
      'Now',
      'Tasks',
      'Habits',
      'Learning',
      'Notes',
      'Practice',
      'Travel',
      'Money',
      'Food',
    ]);
  });

  it('leaves Practice out when its module is off', () => {
    const list = glassNav(user(Permission.General), new Set(['practice']));
    expect(list.map((s) => s.label)).not.toContain('Practice');
  });

  it('leaves Habits out when its module is off', () => {
    const list = glassNav(user(Permission.General), new Set(['habits']));
    expect(list.map((s) => s.label)).not.toContain('Habits');
  });

  it('leaves Now out too when its module is off', () => {
    const list = glassNav(user(Permission.General), new Set(['now']));
    expect(list.map((s) => s.label)).toEqual([
      'Tasks',
      'Goals',
      'Habits',
      'Learning',
      'Notes',
      'Practice',
      'Travel',
      'Money',
      'Food',
    ]);
  });

  it('gives no user only Now', () => {
    expect(glassNav(null).map((s) => s.label)).toEqual(['Now']);
  });
});

describe('homeOf', () => {
  it('is the first section of the nav', () => {
    expect(homeOf(glassNav(user(Permission.General)))).toBe('/now');
    expect(homeOf(glassNav(user(Permission.General), new Set(['now'])))).toBe('/tasks');
  });
  it('is the account page when nothing is on', () => {
    expect(homeOf([])).toBe('/account');
  });
});

describe('DASHBOARD_HOME', () => {
  it('is the Now screen', () => {
    expect(DASHBOARD_HOME).toBe('/now');
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
