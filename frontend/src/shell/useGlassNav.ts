import { useFoodEnabled } from '@/hooks/useFood';
import { useGoalsEnabled } from '@/hooks/useGoals';
import { useHabitsEnabled } from '@/hooks/useHabits';
import { useMe } from '@/hooks/useMe';
import { useMoneyEnabled } from '@/hooks/useMoney';
import { useNotesEnabled } from '@/hooks/useNotes';
import { useNowEnabled } from '@/hooks/useNow';
import { useTasksEnabled } from '@/hooks/useTasks';
import { useTravelEnabled } from '@/hooks/useTravel';
import { useWorkoutEnabled } from '@/hooks/useWorkoutSession';
import { Permission, userCan } from '@/lib/auth/permissions';

import { glassNav, type NavItem } from './sections';

/**
 * The nav items of this user with the switched-off modules left out, and
 * whether the Now module is known yet (undefined while its probe runs).
 */
export function useGlassNav(): { items: NavItem[]; nowOn: boolean | undefined } {
  const user = useMe().data ?? null;
  const signedIn = user !== null;
  const nowOn = useNowEnabled(signedIn);
  const workoutOn = useWorkoutEnabled(userCan(user, Permission.FitnessRead));
  const foodOn = useFoodEnabled(signedIn);
  const moneyOn = useMoneyEnabled(signedIn);
  const tasksOn = useTasksEnabled(signedIn);
  const goalsOn = useGoalsEnabled(signedIn);
  const travelOn = useTravelEnabled(signedIn);
  const habitsOn = useHabitsEnabled(signedIn);
  const notesOn = useNotesEnabled(signedIn);
  const off = new Set<string>();
  if (nowOn === false) off.add('now');
  if (!workoutOn) off.add('workout');
  if (!foodOn) off.add('food');
  if (!moneyOn) off.add('money');
  if (!tasksOn) off.add('tasks');
  if (!goalsOn) off.add('goals');
  if (!travelOn) off.add('travel');
  if (!habitsOn) off.add('habits');
  if (!notesOn) off.add('notes');
  // Learning has no module of its own: its courses are projects of Tasks.
  if (!tasksOn) off.add('learning');
  return { items: glassNav(user, off), nowOn };
}
