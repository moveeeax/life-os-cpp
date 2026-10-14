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
      ['Learning', '/learning'],
      ['Notes', '/notes'],
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
      'Learning',
      'Notes',
      'Travel',
      'Money',
      'Food',
    ]);
  });

  it('shows Body and Training to an administrator through the sentinel bit', () => {
    expect(glassNav(user(Permission.Administer)).map((s) => s.label)).toContain('Training');
  });

  it('leaves out a section whose module is off', () => {
    const list = glassNav(user(Permission.General), new Set(['goals']));
    expect(list.map((s) => s.label)).toEqual([
      'Now',
      'Tasks',
      'Learning',
      'Notes',
      'Travel',
      'Money',
      'Food',
    ]);
  });

  it('leaves Now out too when its module is off', () => {
    const list = glassNav(user(Permission.General), new Set(['now']));
    expect(list.map((s) => s.label)).toEqual([
      'Tasks',
      'Goals',
      'Learning',
      'Notes',
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
