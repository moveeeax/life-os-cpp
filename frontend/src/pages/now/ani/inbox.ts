import { useEffect, useRef, useState } from 'react';

import { api } from '@/lib/api/client';
import type { components } from '@/lib/api/schema.gen';

import { plural } from './greet';
import type { AniOutfit } from './state';

/*
 * What came through her webhook (POST /api/v1/ani/hook/...), played on Now.
 * The page claims ready items every few seconds while she may speak (each
 * item is handed out once) and only counts them while she may not (Hide
 * numbers, a voice call, sent off her ledge): they wait on the server, a dot
 * on her ledge says so.
 */

export type InboxItem = components['schemas']['AniInboxItem'];

export type Play =
  | { kind: 'say'; text: string }
  | { kind: 'outfit'; outfit: AniOutfit }
  | { kind: 'act'; name: string };

export const POLL_MS = 5000;
/** The pause between two of her lines. */
export const GAP_MS = 1500;
/** A move that came longer ago than this means nothing any more. */
const ACT_FRESH_MS = 10 * 60_000;
/** More lines than this at once: she says first that they piled up. */
const BACKLOG_LINES = 3;

const OUTFITS: readonly string[] = ['p1', 'p2', 'lingerie', 'bare'];

/**
 * The items as she plays them, oldest first: moves older than 10 minutes are
 * dropped, only the newest outfit applies, and more than 3 lines open with a
 * line that they piled up while the user was away.
 */
export function planBacklog(items: InboxItem[], now: number): Play[] {
  const lastOutfit = items.reduce(
    (last, it, i) =>
      it.kind === 'outfit' && OUTFITS.includes(String(it.payload.outfit)) ? i : last,
    -1,
  );
  const plan: Play[] = [];
  items.forEach((it, i) => {
    if (it.kind === 'say' || it.kind === 'event') {
      if (it.say) plan.push({ kind: 'say', text: it.say });
    } else if (it.kind === 'outfit') {
      if (i === lastOutfit) plan.push({ kind: 'outfit', outfit: it.payload.outfit as AniOutfit });
    } else if (it.kind === 'act') {
      const name = it.payload.name;
      if (typeof name === 'string' && now - Date.parse(it.created_at) <= ACT_FRESH_MS)
        plan.push({ kind: 'act', name });
    }
  });
  const lines = plan.filter((p) => p.kind === 'say').length;
  if (lines > BACKLOG_LINES) {
    const words = plural(lines, 'сообщение', 'сообщения', 'сообщений');
    plan.unshift({ kind: 'say', text: `Пока тебя не было, пришло ${lines} ${words}` });
  }
  return plan;
}

const wait = (ms: number) => new Promise<void>((ok) => setTimeout(ok, ms));

/**
 * Polls her inbox: while `enabled`, claims and plays (one at a time, a pause
 * between lines); otherwise only counts. `waiting` is the count for the dot.
 */
export function useInbox(enabled: boolean, play: (p: Play) => Promise<void>) {
  const [waiting, setWaiting] = useState(0);
  const playRef = useRef(play);
  playRef.current = play;
  const busy = useRef(false);

  useEffect(() => {
    let gone = false;
    const tick = async () => {
      if (busy.current || gone || document.visibilityState !== 'visible') return;
      busy.current = true;
      try {
        if (!enabled) {
          const r = await api.getJson<{ data: { waiting: number } }>('/api/v1/ani/inbox/count');
          if (!gone) setWaiting(r.data.waiting);
          return;
        }
        const r = await api.postJson<{ data: InboxItem[] }>('/api/v1/ani/inbox/claim');
        if (gone) return;
        setWaiting(0);
        for (const p of planBacklog(r.data, Date.now())) {
          if (gone) return;
          await playRef.current(p);
          if (p.kind === 'say') await wait(GAP_MS);
        }
      } catch {
        // Offline or signed out: the next tick tries again.
      } finally {
        busy.current = false;
      }
    };
    void tick();
    const id = window.setInterval(() => void tick(), POLL_MS);
    const onVisible = () => {
      if (document.visibilityState === 'visible') void tick();
    };
    document.addEventListener('visibilitychange', onVisible);
    return () => {
      gone = true;
      clearInterval(id);
      document.removeEventListener('visibilitychange', onVisible);
    };
  }, [enabled]);

  return { waiting };
}
