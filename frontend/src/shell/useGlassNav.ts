import {
  FOOD_PROBE,
  GOALS_PROBE,
  HABITS_PROBE,
  MONEY_PROBE,
  NOTES_PROBE,
  NOW_PROBE,
  PRACTICE_PROBE,
  TASKS_PROBE,
  TRAVEL_PROBE,
  WORKOUT_PROBE,
} from '@/hooks/moduleProbes';
import { useMe } from '@/hooks/useMe';
import { probesSettled, useModuleOn } from '@/hooks/useModuleProbe';
import { Permission, userCan } from '@/lib/auth/permissions';

import { glassNav, type NavItem } from './sections';

/**
 * The nav items of this user with the switched-off modules left out, whether
 * the Now module is known yet (undefined while it is asked), and whether every
 * module has answered (until then a module still asking counts as on). One
 * GET /api/v1/modules answers for all; the per-module probes run only if it fails.
 */
export function useGlassNav(): {
  items: NavItem[];
  nowOn: boolean | undefined;
  settled: boolean;
} {
  const user = useMe().data ?? null;
  const signedIn = user !== null;
  const canTrain = userCan(user, Permission.FitnessRead);
  const probes = [
    { id: 'now', enabled: signedIn, on: useModuleOn('now', NOW_PROBE, signedIn) },
    { id: 'workout', enabled: canTrain, on: useModuleOn('workout', WORKOUT_PROBE, canTrain) },
    { id: 'food', enabled: signedIn, on: useModuleOn('food', FOOD_PROBE, signedIn) },
    { id: 'money', enabled: signedIn, on: useModuleOn('money', MONEY_PROBE, signedIn) },
    { id: 'tasks', enabled: signedIn, on: useModuleOn('tasks', TASKS_PROBE, signedIn) },
    { id: 'goals', enabled: signedIn, on: useModuleOn('goals', GOALS_PROBE, signedIn) },
    { id: 'travel', enabled: signedIn, on: useModuleOn('travel', TRAVEL_PROBE, signedIn) },
    { id: 'habits', enabled: signedIn, on: useModuleOn('habits', HABITS_PROBE, signedIn) },
    { id: 'notes', enabled: signedIn, on: useModuleOn('notes', NOTES_PROBE, signedIn) },
    {
      id: 'practice',
      enabled: signedIn,
      on: useModuleOn('practice', PRACTICE_PROBE, signedIn),
    },
  ];
  const off = new Set(probes.filter((p) => p.on === false).map((p) => p.id));
  // Learning has no module of its own: its courses are projects of Tasks.
  if (off.has('tasks')) off.add('learning');
  return {
    items: glassNav(user, off),
    nowOn: probes[0].on,
    settled: probesSettled(probes),
  };
}
