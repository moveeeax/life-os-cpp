import { HeartPulse, ShieldAlert } from 'lucide-react';

import { useMe } from '@/hooks/useMe';
import { Permission, userCan } from '@/lib/auth/permissions';

const cardClass =
  'rounded-2xl border border-gray-200 bg-white p-6 dark:border-gray-800 dark:bg-white/3';

/**
 * Health section. The permission is checked here, not by a route guard: the
 * guard's fallback is `/`, which sends a signed-in user straight back.
 */
export function HealthPage() {
  const user = useMe().data ?? null;

  if (!userCan(user, Permission.FitnessRead)) {
    return (
      <div className={cardClass} role="alert">
        <div className="flex items-start gap-3">
          <ShieldAlert className="size-6 shrink-0 text-error-500" aria-hidden="true" />
          <div>
            <h1 className="text-theme-xl font-semibold text-gray-800 dark:text-white/90">
              No access to Health
            </h1>
            <p className="mt-1 text-sm text-gray-500 dark:text-gray-400">
              Your role does not include reading fitness data. Ask an administrator for the Fitness
              Reader role.
            </p>
          </div>
        </div>
      </div>
    );
  }

  return (
    <div className="flex flex-col gap-6">
      <h1 className="text-title-sm font-semibold text-gray-800 dark:text-white/90">Health</h1>
      <div className={cardClass}>
        <div className="flex items-start gap-3">
          <HeartPulse className="size-6 shrink-0 text-brand-500" aria-hidden="true" />
          <div>
            <h2 className="text-theme-xl font-semibold text-gray-800 dark:text-white/90">
              Charts are on the way
            </h2>
            <p className="mt-1 text-sm text-gray-500 dark:text-gray-400">
              Activity, sleep, heart rate, stress and body charts from Mi Fitness, and the sync
              controls, arrive with the next release.
            </p>
          </div>
        </div>
      </div>
    </div>
  );
}
