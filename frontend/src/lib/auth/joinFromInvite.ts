import { api, apiErrorMessage } from '@/lib/api/client';

/** Outcome of accepting an invite — drives the success vs. error branch. */
export type JoinResult = { ok: true } | { ok: false; error: string };

/**
 * Submits the new password for an invite token. Pure enough to unit-test:
 * the page (pages/JoinFromInvite.tsx) just maps the result onto state.
 * Returns ok:true on 2xx, ok:false with a user-facing message otherwise.
 */
export async function submitJoinFromInvite(
  token: string,
  newPassword: string,
): Promise<JoinResult> {
  const { error } = await api.POST(
    '/api/v1/account/join-from-invite/' + encodeURIComponent(token),
    { body: { new_password: newPassword } },
  );
  if (error) {
    return {
      ok: false,
      error: apiErrorMessage(error, 'This invitation link is invalid or has expired.'),
    };
  }
  return { ok: true };
}
