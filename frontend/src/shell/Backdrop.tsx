import { useEffect, useMemo, useRef, useState } from 'react';

import { useWallpapers } from '@/hooks/useNow';
import {
  ANI_WALLPAPER_EVENT,
  deviceOf,
  pickWallpaper,
  switchWallpaper,
  type Pick,
} from '@/lib/now';

function prefersReducedMotion(): boolean {
  return typeof matchMedia === 'function' && matchMedia('(prefers-reduced-motion: reduce)').matches;
}

function savesData(): boolean {
  const c = (navigator as Navigator & { connection?: { saveData?: boolean } }).connection;
  return c?.saveData === true;
}

/**
 * The picture behind every screen: a random wallpaper of this device, picked
 * once per page load and kept while moving between screens. A video fades in
 * over its poster; an error or an empty library leaves the gradient. Ani's
 * webhook or MCP may switch it (ANI_WALLPAPER_EVENT).
 */
export function Backdrop() {
  const device = useMemo(() => deviceOf(window.innerWidth), []);
  const list = useWallpapers(device).data;
  const picked = useRef<Pick | null>(null);
  if (picked.current === null && list !== undefined) {
    picked.current = pickWallpaper(list, device, {
      reducedMotion: prefersReducedMotion(),
      saveData: savesData(),
    });
  }
  const [switched, setSwitched] = useState<Pick | null>(null);
  const pick = switched ?? picked.current;
  const [shown, setShown] = useState(false);
  const [failed, setFailed] = useState(false);
  const now = useRef({ list, pick });
  now.current = { list, pick };
  useEffect(() => {
    const onSwitch = (e: Event) => {
      const { list: all, pick: current } = now.current;
      if (!all) return;
      const which = (e as CustomEvent<{ which?: string }>).detail?.which;
      const next = switchWallpaper(
        all,
        device,
        { reducedMotion: prefersReducedMotion(), saveData: savesData() },
        current?.id ?? null,
        which,
      );
      if (next.src === current?.src) return;
      setShown(false);
      setFailed(false);
      setSwitched(next);
    };
    window.addEventListener(ANI_WALLPAPER_EVENT, onSwitch);
    return () => window.removeEventListener(ANI_WALLPAPER_EVENT, onSwitch);
  }, [device]);

  return (
    <div className="g-backdrop" aria-hidden="true">
      {pick?.kind === 'video' && pick.src && !failed && (
        <>
          {pick.poster && <img src={pick.poster} alt="" className="g-shown" />}
          <video
            key={pick.src}
            src={pick.src}
            poster={pick.poster ?? undefined}
            autoPlay
            muted
            loop
            playsInline
            className={shown ? 'g-shown' : undefined}
            onCanPlay={() => setShown(true)}
            onError={() => setFailed(true)}
          />
        </>
      )}
      {pick?.kind === 'image' && pick.src && !failed && (
        <img
          key={pick.src}
          src={pick.src}
          alt=""
          className={shown ? 'g-shown' : undefined}
          onLoad={() => setShown(true)}
          onError={() => setFailed(true)}
        />
      )}
    </div>
  );
}
