// @vitest-environment jsdom
import { act, renderHook } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import { aniBase, useAni } from './ani';

describe('aniBase', () => {
  it('puts Ani next to the wallpapers in the bucket', () => {
    expect(aniBase('https://s3.example.com/life-os/life-os/wallpapers/0b7c/file.mp4')).toBe(
      'https://s3.example.com/life-os/life-os/wallpapers/ani/',
    );
  });

  it('has no place without a wallpaper URL to take it from', () => {
    expect(aniBase(undefined)).toBeNull();
    expect(aniBase('https://cdn.example.com/other/file.jpg')).toBeNull();
  });
});

describe('useAni', () => {
  it('is off by default, here and in the dress, and remembers a change', () => {
    const { result } = renderHook(() => useAni());
    expect(result.current[0]).toEqual({ on: false, here: true, outfit: 'p1' });
    act(() => result.current[1]({ on: true, outfit: 'p2' }));
    expect(result.current[0]).toEqual({ on: true, here: true, outfit: 'p2' });
    expect(localStorage.getItem('now:ani')).toBe('1');
    expect(localStorage.getItem('now:ani:outfit')).toBe('p2');
    act(() => result.current[1]({ on: false, outfit: 'p1' }));
  });
});

describe('album', () => {
  it('HerAlbum: keeps each moment once, newest first', async () => {
    const { addMoment, readAlbum } = await import('./ani');
    localStorage.clear();
    addMoment('2026-10-30', 'Десять часов фокуса за неделю');
    addMoment('2026-10-31', 'Уговор выполнен: Зал');
    addMoment('2026-10-31', 'Уговор выполнен: Зал');
    expect(readAlbum()).toEqual([
      { date: '2026-10-31', title: 'Уговор выполнен: Зал' },
      { date: '2026-10-30', title: 'Десять часов фокуса за неделю' },
    ]);
  });
});
