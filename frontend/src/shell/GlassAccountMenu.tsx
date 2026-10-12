import { useEffect, useRef, useState } from 'react';
import { Link, useNavigate } from 'react-router';

import { useToast } from '@/components/ui/toaster';
import { useLogout } from '@/hooks/useAuthMutations';
import { useMe } from '@/hooks/useMe';
import { Permission, userCan } from '@/lib/auth/permissions';

/** The account on the right of the bar: Account, Admin, Billing, Sign out. */
export function GlassAccountMenu() {
  const user = useMe().data ?? null;
  const logout = useLogout();
  const toast = useToast();
  const navigate = useNavigate();
  const [open, setOpen] = useState(false);
  const ref = useRef<HTMLDivElement>(null);
  const buttonRef = useRef<HTMLButtonElement>(null);

  useEffect(() => {
    if (!open) return;
    const onDown = (e: MouseEvent) => {
      if (ref.current && !ref.current.contains(e.target as Node)) setOpen(false);
    };
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        setOpen(false);
        buttonRef.current?.focus();
      }
    };
    document.addEventListener('mousedown', onDown);
    document.addEventListener('keydown', onKey);
    return () => {
      document.removeEventListener('mousedown', onDown);
      document.removeEventListener('keydown', onKey);
    };
  }, [open]);

  if (!user) return null;
  const name = user.full_name || user.email;

  const signOut = async () => {
    setOpen(false);
    try {
      await logout.mutateAsync();
      navigate('/', { replace: true });
    } catch {
      toast.error('Could not sign out. Check the connection and try again.');
    }
  };

  return (
    <div
      className="g-account"
      ref={ref}
      onBlur={(e) => {
        if (!e.currentTarget.contains(e.relatedTarget)) setOpen(false);
      }}
    >
      <button
        ref={buttonRef}
        type="button"
        className="g-pill"
        aria-label={`Account: ${name}`}
        aria-expanded={open}
        aria-controls="glass-account-menu"
        onClick={() => setOpen((v) => !v)}
      >
        {name.slice(0, 1).toUpperCase()}
      </button>
      {open && (
        <div id="glass-account-menu" className="g-menu">
          <div className="g-who">{user.email}</div>
          <Link to="/account" onClick={() => setOpen(false)}>
            Account
          </Link>
          {userCan(user, Permission.Administer) && (
            <Link to="/admin" onClick={() => setOpen(false)}>
              Admin
            </Link>
          )}
          <Link to="/billing" onClick={() => setOpen(false)}>
            Billing
          </Link>
          <button type="button" onClick={signOut}>
            Sign out
          </button>
        </div>
      )}
    </div>
  );
}
