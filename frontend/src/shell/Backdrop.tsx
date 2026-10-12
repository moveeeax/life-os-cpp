import { useMemo, useRef, useState } from 'react';

import { useWallpapers } from '@/hooks/useNow';
import { deviceOf, pickWallpaper, type Pick } from '@/lib/now';

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
 * over its poster; an error or an empty library leaves the gradient.
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
  const pick = picked.current;
  const [shown, setShown] = useState(false);
  const [failed, setFailed] = useState(false);

  return (
    <div className="g-backdrop" aria-hidden="true">
      {pick?.kind === 'video' && pick.src && !failed && (
        <>
          {pick.poster && <img src={pick.poster} alt="" className="g-shown" />}
          <video
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
