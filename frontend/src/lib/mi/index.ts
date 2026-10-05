// Pure logic of the Mi account block: no React, no fetch.
import type { components } from '@/lib/api/schema.gen';

export type MiStatus = components['schemas']['MiAccountStatus'];
export type MiLinkStart = components['schemas']['MiLinkStart'];
export type MiLinkStep = components['schemas']['MiLinkStep'];

/** Where a link attempt of this page is. */
export type LinkState = 'idle' | 'starting' | 'waiting' | 'linked' | 'expired' | 'failed';

/** What the profile block shows for a status answer. */
export type MiView = 'none' | 'linked' | 'relink';

export function miView(status: MiStatus | undefined): MiView {
  if (status?.status === 'ok') return 'linked';
  if (status?.status === 'reauth_required') return 'relink';
  return 'none';
}

/**
 * True when the user has no linked account. False while the status is not
 * known (loading, or the request failed): a hiccup must not tell a linked
 * user to link.
 */
export const needsLink = (status: MiStatus | undefined): boolean => status?.status === 'none';

/** Whole seconds until `expiresAt`; 0 when it has passed or is not a date. */
export function secondsLeft(expiresAt: string, now: number): number {
  const at = Date.parse(expiresAt);
  return Number.isNaN(at) ? 0 : Math.max(0, Math.ceil((at - now) / 1000));
}

/** Why a link attempt failed, for the user. */
export function linkErrorText(code: string | undefined): string {
  switch (code) {
    case 'account_linked_elsewhere':
      return 'This Xiaomi account is already linked to another Life OS user.';
    case 'different_account':
      return 'You confirmed a different Xiaomi account than the one linked here. Unlink the current account first.';
    case 'xiaomi_refused':
      return 'Xiaomi did not accept the sign-in. Try again.';
    case 'xiaomi_unavailable':
      return 'Xiaomi confirmed the sign-in but did not answer afterwards. Try again in a minute.';
    case 'rate_limited':
      return 'Too many attempts. Wait ten minutes and try again.';
    case 'not_configured':
      return 'Linking is not set up on this server.';
    case 'upstream_unavailable':
      return 'Xiaomi sign-in is not available right now. Try again in a minute.';
    default:
      return 'Linking failed. Try again.';
  }
}

/** Why a linked account asks to be linked again. */
export function reauthReason(lastError: string | null | undefined): string {
  return lastError === 'upstream_auth'
    ? 'Xiaomi no longer accepts the saved sign-in.'
    : 'The saved sign-in stopped working.';
}

/** Mi Fitness cloud regions, in the order the server probes them. */
export const REGIONS: { value: NonNullable<MiStatus['region']>; label: string }[] = [
  { value: 'cn', label: 'China (cn)' },
  { value: 'sg', label: 'Singapore (sg)' },
  { value: 'de', label: 'Europe (de)' },
  { value: 'ru', label: 'Russia (ru)' },
  { value: 'us', label: 'United States (us)' },
  { value: 'i2', label: 'India (i2)' },
];

export const regionLabel = (region: string | undefined): string =>
  REGIONS.find((r) => r.value === region)?.label ?? region ?? '–';

/** "2:05" for a countdown. */
export function formatCountdown(seconds: number): string {
  const s = Math.max(0, Math.floor(seconds));
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
}
