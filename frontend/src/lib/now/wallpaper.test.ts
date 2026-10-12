import { describe, expect, it } from 'vitest';

import { deviceOf, pickWallpaper } from './wallpaper';
import type { Wallpaper } from './types';

const video: Wallpaper = {
  id: 'v',
  kind: 'video',
  url: 'v.mp4',
  poster_url: 'v.jpg',
  width: 1920,
  height: 1080,
};
const video2: Wallpaper = {
  id: 'w',
  kind: 'video',
  url: 'w.mp4',
  poster_url: 'w.jpg',
  width: 1920,
  height: 1080,
};
const image: Wallpaper = {
  id: 'i',
  kind: 'image',
  url: 'i.jpg',
  poster_url: null,
  width: 1080,
  height: 1920,
};
const calm = { reducedMotion: false, saveData: false };

describe('deviceOf', () => {
  it('treats 768 px and below as a phone', () => {
    expect(deviceOf(390)).toBe('phone');
    expect(deviceOf(768)).toBe('phone');
    expect(deviceOf(769)).toBe('desktop');
  });
});

describe('pickWallpaper', () => {
  it('plays a video on a desktop', () => {
    expect(pickWallpaper([video], 'desktop', calm)).toEqual({
      kind: 'video',
      src: 'v.mp4',
      poster: 'v.jpg',
    });
  });

  it('picks with the given random source', () => {
    expect(pickWallpaper([video, video2], 'desktop', calm, () => 0.99).src).toBe('w.mp4');
    expect(pickWallpaper([video, video2], 'desktop', calm, () => 0).src).toBe('v.mp4');
  });

  it('shows the poster when motion is reduced or data is saved', () => {
    expect(pickWallpaper([video], 'desktop', { reducedMotion: true, saveData: false })).toEqual({
      kind: 'image',
      src: 'v.jpg',
      poster: null,
    });
    expect(pickWallpaper([video], 'desktop', { reducedMotion: false, saveData: true }).kind).toBe(
      'image',
    );
  });

  it('never plays a video on a phone, even from a desktop list', () => {
    expect(pickWallpaper([video, video2], 'phone', calm)).toEqual({
      kind: 'none',
      src: null,
      poster: null,
    });
  });

  it('shows a phone image on a phone', () => {
    expect(pickWallpaper([image], 'phone', calm)).toEqual({
      kind: 'image',
      src: 'i.jpg',
      poster: null,
    });
  });

  it('falls back to nothing on an empty list', () => {
    expect(pickWallpaper([], 'desktop', calm)).toEqual({ kind: 'none', src: null, poster: null });
  });
});
