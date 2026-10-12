import { describe, expect, it } from 'vitest';

import { LIMITS, checkAudio, checkFile } from './upload';

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

describe('checkAudio', () => {
  it('takes MP3, AAC/M4A and Ogg up to 30 MB', () => {
    expect(checkAudio({ type: 'audio/mpeg', size: 8 * MB })).toEqual({ ok: true });
    expect(checkAudio({ type: 'audio/x-m4a', size: LIMITS.sound })).toEqual({ ok: true });
    expect(checkAudio({ type: 'audio/ogg', size: 1 })).toEqual({ ok: true });
  });
  it('refuses other types, empty files and files over 30 MB', () => {
    expect(checkAudio({ type: 'audio/wav', size: MB }).ok).toBe(false);
    expect(checkAudio({ type: 'audio/mpeg', size: 0 }).ok).toBe(false);
    expect(checkAudio({ type: 'audio/mpeg', size: LIMITS.sound + 1 }).ok).toBe(false);
  });
});
