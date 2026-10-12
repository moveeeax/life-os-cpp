import { describe, expect, it } from 'vitest';

import { LIMITS, checkFile } from './upload';

const MB = 1024 * 1024;

describe('checkFile', () => {
  it('takes a video for a desktop', () => {
    expect(checkFile({ type: 'video/mp4', size: 40 * MB }, 'desktop')).toEqual({
      ok: true,
      kind: 'video',
    });
    expect(checkFile({ type: 'video/webm', size: LIMITS.video }, 'desktop')).toEqual({
      ok: true,
      kind: 'video',
    });
  });

  it('refuses a video for a phone', () => {
    const r = checkFile({ type: 'video/mp4', size: MB }, 'phone');
    expect(r.ok).toBe(false);
    if (!r.ok) expect(r.reason).toMatch(/phone/);
  });

  it('refuses what the library does not take', () => {
    expect(checkFile({ type: 'image/png', size: MB }, 'desktop').ok).toBe(false);
    expect(checkFile({ type: 'video/quicktime', size: MB }, 'desktop').ok).toBe(false);
  });

  it('refuses a file over its limit, by one byte', () => {
    expect(checkFile({ type: 'video/mp4', size: LIMITS.video + 1 }, 'desktop').ok).toBe(false);
    expect(checkFile({ type: 'image/jpeg', size: LIMITS.image + 1 }, 'phone').ok).toBe(false);
    expect(checkFile({ type: 'image/webp', size: LIMITS.image }, 'phone')).toEqual({
      ok: true,
      kind: 'image',
    });
  });

  it('refuses an empty file', () => {
    expect(checkFile({ type: 'image/jpeg', size: 0 }, 'phone').ok).toBe(false);
  });
});
