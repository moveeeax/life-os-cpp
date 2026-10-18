import {
  useCallback,
  useEffect,
  useRef,
  useState,
  type PointerEvent as ReactPointerEvent,
} from 'react';

/** How far a pointer travels before a press on a tile becomes a drag, in px. */
export const DRAG_SLOP = 6;

/** How long a finger rests on a tile before it lifts for a drag, in ms. */
export const LONG_PRESS_MS = 300;

/** The dock position whose tile centre is nearest to @p pointer on @p axis. */
export function dropIndex(rects: DOMRect[], axis: 'x' | 'y', pointer: number): number {
  let best = 0;
  let bestDist = Number.POSITIVE_INFINITY;
  rects.forEach((r, i) => {
    const centre = axis === 'x' ? r.left + r.width / 2 : r.top + r.height / 2;
    const d = Math.abs(centre - pointer);
    if (d < bestDist) {
      bestDist = d;
      best = i;
    }
  });
  return best;
}

interface Drag {
  from: number;
  pointerId: number;
  startX: number;
  startY: number;
  /** False while a finger has not rested long enough: a move then scrolls the page. */
  armed: boolean;
  /** True once the pointer left the slop: the press is a drag, not a click. */
  moving: boolean;
  /** Where the dragged tile would land right now. */
  over: number;
  /** Offset of the dragged tile along the axis, in px. */
  delta: number;
}

/**
 * Drag a dock tile to another position. Pointer events on the tiles: a press
 * that moves under DRAG_SLOP stays a click; beyond it the tile follows the
 * pointer along the dock's axis (horizontal on a desktop, vertical on a
 * phone), the others make room, and on release @p onReorder gets the move.
 * A finger has to rest LONG_PRESS_MS on a tile first, so a swipe over the
 * dock still scrolls the page.
 */
export function useDockDrag(
  dock: React.RefObject<HTMLElement | null>,
  onReorder: (from: number, to: number) => void,
) {
  const [drag, setDrag] = useState<Drag | null>(null);
  const state = useRef<Drag | null>(null);
  // The click event lands after pointerup: this remembers that it ends a drag.
  const endedDrag = useRef(false);
  const rects = useRef<DOMRect[]>([]);
  const axis = useRef<'x' | 'y'>('x');
  const timer = useRef<number | undefined>(undefined);
  const reorder = useRef(onReorder);
  reorder.current = onReorder;

  const tiles = useCallback(
    () => [...(dock.current?.querySelectorAll<HTMLElement>('.n-tile') ?? [])],
    [dock],
  );

  const onPointerDown = (index: number) => (e: ReactPointerEvent<HTMLButtonElement>) => {
    if (e.button !== 0 && e.pointerType === 'mouse') return;
    axis.current = window.matchMedia('(max-width: 640px)').matches ? 'y' : 'x';
    rects.current = tiles().map((t) => t.getBoundingClientRect());
    const touch = e.pointerType === 'touch';
    const d: Drag = {
      from: index,
      pointerId: e.pointerId,
      startX: e.clientX,
      startY: e.clientY,
      armed: !touch,
      moving: false,
      over: index,
      delta: 0,
    };
    state.current = d;
    window.clearTimeout(timer.current);
    if (touch)
      timer.current = window.setTimeout(() => {
        if (state.current?.pointerId === d.pointerId) {
          state.current = { ...state.current, armed: true };
          navigator.vibrate?.(10);
        }
      }, LONG_PRESS_MS);
    e.currentTarget.setPointerCapture(e.pointerId);
  };

  useEffect(() => {
    const move = (e: PointerEvent) => {
      const d = state.current;
      if (!d || e.pointerId !== d.pointerId) return;
      const dx = e.clientX - d.startX;
      const dy = e.clientY - d.startY;
      if (!d.moving && Math.hypot(dx, dy) < DRAG_SLOP) return;
      if (!d.armed) {
        // A finger moved before the long press: the page scrolls, no drag.
        window.clearTimeout(timer.current);
        state.current = null;
        return;
      }
      const along = axis.current === 'x' ? e.clientX : e.clientY;
      const next: Drag = {
        ...d,
        moving: true,
        delta: axis.current === 'x' ? dx : dy,
        over: dropIndex(rects.current, axis.current, along),
      };
      state.current = next;
      setDrag(next);
    };
    const up = (e: PointerEvent) => {
      const d = state.current;
      if (!d || e.pointerId !== d.pointerId) return;
      window.clearTimeout(timer.current);
      state.current = null;
      endedDrag.current = d.moving;
      setDrag(null);
      if (d.moving && d.over !== d.from) reorder.current(d.from, d.over);
    };
    // A lifted tile keeps the page still: touch-action is fixed at touchstart,
    // so the scroll is stopped here instead.
    const hold = (e: TouchEvent) => {
      if (state.current?.armed) e.preventDefault();
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
    window.addEventListener('pointercancel', up);
    window.addEventListener('touchmove', hold, { passive: false });
    return () => {
      window.clearTimeout(timer.current);
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
      window.removeEventListener('pointercancel', up);
      window.removeEventListener('touchmove', hold);
    };
  }, []);

  /** The transform of tile @p index while a drag is on. */
  const styleOf = (index: number): React.CSSProperties | undefined => {
    if (!drag?.moving) return undefined;
    const a = axis.current;
    const size = (i: number) =>
      a === 'x' ? (rects.current[i]?.width ?? 0) : (rects.current[i]?.height ?? 0);
    const shift = (px: number) => (a === 'x' ? `translateX(${px}px)` : `translateY(${px}px)`);
    if (index === drag.from) return { transform: shift(drag.delta), zIndex: 1, transition: 'none' };
    // The tiles between the origin and the target slide one place.
    if (drag.from < drag.over && index > drag.from && index <= drag.over)
      return { transform: shift(-size(drag.from)) };
    if (drag.from > drag.over && index >= drag.over && index < drag.from)
      return { transform: shift(size(drag.from)) };
    return undefined;
  };

  return {
    /** True while a tile follows the pointer. */
    dragging: drag?.moving ?? false,
    /** Whether the click being handled ends a drag (then it opens nothing); clears itself. */
    consumeClick: () => {
      const was = endedDrag.current;
      endedDrag.current = false;
      return was;
    },
    onPointerDown,
    styleOf,
  };
}
