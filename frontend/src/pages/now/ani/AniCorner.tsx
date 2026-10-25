import { useEffect, useRef, useState } from 'react';

import type { Ani } from './engine';
import { useAni, type AniOutfit } from './state';
import './ani.css';

/** A hanger: opens the outfits in place of the ledge's state. */
function HangerIcon() {
  return (
    <svg
      viewBox="0 0 24 24"
      width="17"
      height="17"
      aria-hidden="true"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.8"
      strokeLinecap="round"
      strokeLinejoin="round"
    >
      <path d="M9.5 6.5a2.5 2.5 0 1 1 3.6 2.2c-.7.4-1.1 1-1.1 1.8v.7" />
      <path d="M12 11.2 3.4 16.6A1.5 1.5 0 0 0 4.2 19.4h15.6a1.5 1.5 0 0 0 .8-2.8z" />
    </svg>
  );
}

const OUTFITS: { id: AniOutfit; label: string }[] = [
  { id: 'p1', label: 'Dress' },
  { id: 'p2', label: 'Slip' },
  { id: 'lingerie', label: 'Lingerie' },
];

/**
 * Ani in the bottom left corner of Now: the model standing on a glass ledge
 * left of the dock. The ledge sends her off through the left edge and calls
 * her back; its hanger opens the outfits, and a change happens off screen.
 * A drag on her turns her round, a tap plays the greeting. The engine (three
 * and the model, about 15 MB from the bucket) loads only when she is on.
 */
export function AniCorner({ base }: { base: string }) {
  const [ani, setAni] = useAni();
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const hitRef = useRef<HTMLDivElement>(null);
  const engine = useRef<Ani | null>(null);
  const [ready, setReady] = useState(false);
  const [failed, setFailed] = useState(false);
  const [busy, setBusy] = useState<'leaving' | 'arriving' | 'changing' | null>(null);
  const [wardrobe, setWardrobe] = useState(false);
  const ledgeRef = useRef<HTMLDivElement>(null);
  // The first state the engine starts from; later changes go through calls.
  const initial = useRef(ani);

  useEffect(() => {
    const canvas = canvasRef.current;
    const hit = hitRef.current;
    if (!canvas || !hit) return;
    let gone = false;
    let made: Ani | null = null;
    void import('./engine').then(({ mountAni }) => {
      if (gone) return;
      try {
        made = mountAni(canvas, {
          base,
          hit,
          anchor: 110,
          outfit: initial.current.outfit,
          present: initial.current.here,
          onReady: () => setReady(true),
          onError: () => setFailed(true),
        });
        engine.current = made;
      } catch {
        // No WebGL: the corner stays empty.
        setFailed(true);
      }
    });
    return () => {
      gone = true;
      made?.dispose();
      engine.current = null;
    };
  }, [base]);

  useEffect(() => {
    if (!wardrobe) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') setWardrobe(false);
    };
    const onDown = (e: PointerEvent) => {
      if (!ledgeRef.current?.contains(e.target as Node)) setWardrobe(false);
    };
    document.addEventListener('keydown', onKey);
    document.addEventListener('pointerdown', onDown);
    return () => {
      document.removeEventListener('keydown', onKey);
      document.removeEventListener('pointerdown', onDown);
    };
  }, [wardrobe]);

  if (failed) return null;

  const toggle = () => {
    if (busy || !engine.current) return;
    const here = !ani.here;
    setAni({ here });
    setBusy(here ? 'arriving' : 'leaving');
    engine.current.present(here, () => setBusy(null));
  };

  const wear = (o: AniOutfit) => {
    setWardrobe(false);
    const e = engine.current;
    if (busy || !e || o === ani.outfit) return;
    setAni({ outfit: o });
    if (!ani.here) {
      // Away: she comes back already changed.
      e.setOutfit(o);
      setAni({ here: true });
      setBusy('arriving');
      e.present(true, () => setBusy(null));
      return;
    }
    setBusy('changing');
    e.present(false, () => {
      e.setOutfit(o);
      e.present(true, () => setBusy(null));
    });
  };

  const words =
    busy === 'changing'
      ? ['Changing', 'Back in a moment']
      : ani.here
        ? ['With you', 'Send her off']
        : ['Away', 'Call her back'];

  return (
    <div className="ani-corner" data-ready={ready ? '' : undefined}>
      <canvas ref={canvasRef} className="ani-canvas" aria-hidden="true" />
      <div
        ref={hitRef}
        className="ani-hit"
        data-away={ani.here ? undefined : ''}
        role="img"
        aria-label="Ani. Drag to turn her, tap to say hello."
      />
      <div ref={ledgeRef} className="ani-ledge" data-wardrobe={wardrobe ? '' : undefined}>
        {wardrobe ? (
          <div className="ani-wardrobe" role="group" aria-label="Outfit">
            <span className="ani-k">Outfit</span>
            {OUTFITS.map((o) => (
              <button
                key={o.id}
                type="button"
                aria-pressed={ani.outfit === o.id}
                onClick={() => wear(o.id)}
              >
                <span className={`ani-sw ani-sw-${o.id}`} />
                {o.label}
                <span className="ani-tick" aria-hidden="true">
                  ✓
                </span>
              </button>
            ))}
            <span className="ani-credit" data-num-ok>
              Animation credits to pixiv Inc.&apos;s VRoid Project
            </span>
          </div>
        ) : (
          <button
            type="button"
            className="ani-main"
            aria-pressed={ani.here}
            aria-label={ani.here ? 'Ani: send away' : 'Ani: call back'}
            disabled={!ready}
            onClick={toggle}
          >
            <span className="ani-k">
              <span className="ani-dot" />
              Ani
            </span>
            <span className="ani-v">{ready ? words[0] : 'Coming'}</span>
            <span className="ani-s">{ready ? words[1] : 'Loading'}</span>
          </button>
        )}
        <button
          type="button"
          className="ani-hanger"
          aria-label="Outfit"
          aria-expanded={wardrobe}
          disabled={!ready}
          onClick={() => setWardrobe((v) => !v)}
        >
          <HangerIcon />
        </button>
      </div>
    </div>
  );
}
