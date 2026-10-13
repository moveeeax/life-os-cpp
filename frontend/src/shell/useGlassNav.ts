import { useFoodEnabled } from '@/hooks/useFood';
import { useGoalsEnabled } from '@/hooks/useGoals';
import { useMe } from '@/hooks/useMe';
import { useMoneyEnabled } from '@/hooks/useMoney';
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
  const off = new Set<string>();
  if (nowOn === false) off.add('now');
  if (!workoutOn) off.add('workout');
  if (!foodOn) off.add('food');
  if (!moneyOn) off.add('money');
  if (!tasksOn) off.add('tasks');
  if (!goalsOn) off.add('goals');
  if (!travelOn) off.add('travel');
  return { items: glassNav(user, off), nowOn };
}
