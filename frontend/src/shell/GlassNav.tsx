import { NavLink } from 'react-router';

import { useFoodEnabled } from '@/hooks/useFood';
import { useGoalsEnabled } from '@/hooks/useGoals';
import { useMe } from '@/hooks/useMe';
import { useMoneyEnabled } from '@/hooks/useMoney';
import { useTasksEnabled } from '@/hooks/useTasks';
import { useWorkoutEnabled } from '@/hooks/useWorkoutSession';
import { Permission, userCan } from '@/lib/auth/permissions';

import { glassNav } from './sections';

/** The glass pill nav; a section whose module is off is left out. */
export function GlassNav() {
  const user = useMe().data ?? null;
  const signedIn = user !== null;
  const workoutOn = useWorkoutEnabled(userCan(user, Permission.FitnessRead));
  const foodOn = useFoodEnabled(signedIn);
  const moneyOn = useMoneyEnabled(signedIn);
  const tasksOn = useTasksEnabled(signedIn);
  const goalsOn = useGoalsEnabled(signedIn);
  const off = new Set<string>();
  if (!workoutOn) off.add('workout');
  if (!foodOn) off.add('food');
  if (!moneyOn) off.add('money');
  if (!tasksOn) off.add('tasks');
  if (!goalsOn) off.add('goals');

  return (
    <nav className="g-pill g-nav" aria-label="Life OS">
      {glassNav(user, off).map((s) => (
        <NavLink key={s.key} to={s.to} end={s.to === '/now'}>
          {s.label}
        </NavLink>
      ))}
    </nav>
  );
}
