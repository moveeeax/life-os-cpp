import { Link } from 'react-router';
import { Watch } from 'lucide-react';

interface LinkMiNoticeProps {
  /** What the user gets by linking, e.g. "to see your steps, sleep and heart rate". */
  purpose: string;
  /** Tighter variant for use inside another card. */
  compact?: boolean;
}

/**
 * Shown on dashboard pages to a user who has not linked a Mi account yet:
 * there is no band data to show, and the profile is where linking happens.
 */
export function LinkMiNotice({ purpose, compact }: LinkMiNoticeProps) {
  return (
    <div className={compact ? 'flex flex-wrap items-center gap-3' : 'k-banner'}>
      <Watch className="k-inline-icon" aria-hidden="true" />
      <span className="k-grow">Link your Mi account {purpose}.</span>
      <Link to="/account">Link in profile →</Link>
    </div>
  );
}
