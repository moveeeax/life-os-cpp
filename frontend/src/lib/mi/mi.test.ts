import { describe, expect, it } from 'vitest';

import {
  REGIONS,
  formatCountdown,
  linkErrorText,
  miView,
  needsLink,
  reauthReason,
  regionLabel,
  secondsLeft,
} from './index';

describe('miView and needsLink', () => {
  it('maps each status to what the block shows', () => {
    expect(miView({ status: 'none' })).toBe('none');
    expect(miView({ status: 'ok' })).toBe('linked');
    expect(miView({ status: 'reauth_required' })).toBe('relink');
    expect(miView(undefined)).toBe('none');
  });

  it('asks to link only when the server said there is no link', () => {
    expect(needsLink({ status: 'none' })).toBe(true);
    expect(needsLink({ status: 'ok' })).toBe(false);
    expect(needsLink({ status: 'reauth_required' })).toBe(false);
    // Loading or a failed request: unknown, so no call to link.
    expect(needsLink(undefined)).toBe(false);
  });
});

describe('secondsLeft', () => {
  const at = '2026-10-05T12:05:00+00:00';
  const t = Date.parse(at);

  it('counts whole seconds up and stops at zero', () => {
    expect(secondsLeft(at, t - 300_000)).toBe(300);
    expect(secondsLeft(at, t - 500)).toBe(1);
    expect(secondsLeft(at, t)).toBe(0);
    expect(secondsLeft(at, t + 60_000)).toBe(0);
  });

  it('treats an unreadable deadline as passed', () => {
    expect(secondsLeft('soon', 0)).toBe(0);
    expect(secondsLeft('', 0)).toBe(0);
  });
});

describe('texts', () => {
  it('has its own sentence for every failure the server reports', () => {
    const codes = [
      'account_linked_elsewhere',
      'different_account',
      'xiaomi_refused',
      'xiaomi_unavailable',
      'rate_limited',
      'not_configured',
      'upstream_unavailable',
    ];
    const texts = codes.map(linkErrorText);
    expect(new Set(texts).size).toBe(codes.length);
    const generic = linkErrorText('something_new');
    expect(texts).not.toContain(generic);
    expect(linkErrorText(undefined)).toBe(generic);
  });

  it('explains why a link has to be made again', () => {
    expect(reauthReason('upstream_auth')).toMatch(/no longer accepts/);
    expect(reauthReason(null)).toMatch(/stopped working/);
    expect(reauthReason('other')).toMatch(/stopped working/);
  });

  it('names every region the server knows and falls back to the code', () => {
    expect(REGIONS.map((r) => r.value).sort()).toEqual(['cn', 'de', 'i2', 'ru', 'sg', 'us']);
    expect(regionLabel('sg')).toBe('Singapore (sg)');
    expect(regionLabel('xx')).toBe('xx');
    expect(regionLabel(undefined)).toBe('–');
  });

  it('formats the countdown', () => {
    expect(formatCountdown(300)).toBe('5:00');
    expect(formatCountdown(65)).toBe('1:05');
    expect(formatCountdown(0)).toBe('0:00');
    expect(formatCountdown(-3)).toBe('0:00');
  });
});
