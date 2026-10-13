import type { ReactNode } from 'react';
import { ShieldAlert } from 'lucide-react';

import { useMe } from '@/hooks/useMe';
import { Permission, userCan } from '@/lib/auth/permissions';

import { SectionFrame, type TabItem } from '@/shell/kit';
import { LoadError as KitLoadError } from '@/shell/kit';

const TABS: TabItem[] = [
  { to: '/workout', label: 'Overview', end: true },
  { to: '/workout/routines', label: 'Routines' },
  { to: '/workout/exercises', label: 'Exercises' },
  { to: '/workout/history', label: 'History' },
];

interface WorkoutFrameProps {
  /** The page's own name; the headline when the page gives none. */
  title: string;
  headline?: ReactNode;
  sub?: ReactNode;
  /** The tools line. */
  actions?: ReactNode;
  children: ReactNode;
}

/**
 * Frame of every Training page on the calm sheet, with the permission check.
 * The permission is checked here, not by a route guard, for the same reason as
 * on the Body page.
 */
export function WorkoutFrame({ title, headline, sub, actions, children }: WorkoutFrameProps) {
  const user = useMe().data ?? null;
  if (!userCan(user, Permission.FitnessRead)) {
    return (
      <SectionFrame eyebrow="Training" headline="No access to Training">
        <div className="k-banner" role="alert">
          <span>
            <ShieldAlert className="k-inline-icon" aria-hidden="true" /> Your role does not include
            reading fitness data. Ask an administrator for the Fitness Reader role.
          </span>
        </div>
      </SectionFrame>
    );
  }
  return (
    <SectionFrame
      eyebrow="Training"
      headline={headline ?? title}
      sub={sub}
      tabs={TABS}
      tools={actions}
    >
      {children}
    </SectionFrame>
  );
}

/** A failed query of this section (a 404 names the module). */
export function LoadError({ error, onRetry }: { error: unknown; onRetry: () => void }) {
  return <KitLoadError error={error} onRetry={onRetry} module="workout" />;
}

export { Placeholder } from '@/shell/kit';

/** Small text under a field with an error. */
export function FieldError({ id, message }: { id: string; message?: string }) {
  if (!message) return null;
  return (
    <p id={id} className="k-error">
      {message}
    </p>
  );
}
