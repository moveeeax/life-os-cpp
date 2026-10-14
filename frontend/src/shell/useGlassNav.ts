import { FOOD_PROBE } from '@/hooks/useFood';
import { GOALS_PROBE } from '@/hooks/useGoals';
import { HABITS_PROBE } from '@/hooks/useHabits';
import { useMe } from '@/hooks/useMe';
import { probesSettled, useModuleProbe } from '@/hooks/useModuleProbe';
import { MONEY_PROBE } from '@/hooks/useMoney';
import { NOTES_PROBE } from '@/hooks/useNotes';
import { NOW_PROBE } from '@/hooks/useNow';
import { TASKS_PROBE } from '@/hooks/useTasks';
import { TRAVEL_PROBE } from '@/hooks/useTravel';
import { WORKOUT_PROBE } from '@/hooks/useWorkoutSession';
import { Permission, userCan } from '@/lib/auth/permissions';

import { glassNav, type NavItem } from './sections';

/**
 * The nav items of this user with the switched-off modules left out, whether
 * the Now module is known yet (undefined while its probe runs), and whether
 * every probe has answered (until then a module still asking counts as on).
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
    { id: 'now', enabled: signedIn, on: useModuleProbe(...NOW_PROBE, signedIn) },
    { id: 'workout', enabled: canTrain, on: useModuleProbe(...WORKOUT_PROBE, canTrain) },
    { id: 'food', enabled: signedIn, on: useModuleProbe(...FOOD_PROBE, signedIn) },
    { id: 'money', enabled: signedIn, on: useModuleProbe(...MONEY_PROBE, signedIn) },
    { id: 'tasks', enabled: signedIn, on: useModuleProbe(...TASKS_PROBE, signedIn) },
    { id: 'goals', enabled: signedIn, on: useModuleProbe(...GOALS_PROBE, signedIn) },
    { id: 'travel', enabled: signedIn, on: useModuleProbe(...TRAVEL_PROBE, signedIn) },
    { id: 'habits', enabled: signedIn, on: useModuleProbe(...HABITS_PROBE, signedIn) },
    { id: 'notes', enabled: signedIn, on: useModuleProbe(...NOTES_PROBE, signedIn) },
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
