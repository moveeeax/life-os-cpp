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
  | { kind: 'act'; name: string }
  | { kind: 'mood'; level: -1 | 0 | 1; seconds: number }
  | { kind: 'card'; title: string; text?: string; url?: string; seconds: number }
  | { kind: 'ask'; id: string; text: string; options: string[] }
  | { kind: 'focus'; action: 'start' | 'stop'; minutes?: number }
  | { kind: 'wallpaper'; which?: string }
  | { kind: 'sound'; which?: string };

export const POLL_MS = 5000;
/** The pause between two of her lines. */
export const GAP_MS = 1500;
/** A move or a card that came longer ago than this means nothing any more. */
const ACT_FRESH_MS = 10 * 60_000;
/** More lines than this at once: she says first that they piled up. */
const BACKLOG_LINES = 3;

const OUTFITS: readonly string[] = ['p1', 'p2', 'lingerie', 'bare'];

const str = (v: unknown) => (typeof v === 'string' ? v : undefined);

/**
 * The items as she plays them, oldest first: moves, cards, moods, focus,
 * wallpaper and sound older than 10 minutes are dropped, only the newest outfit, mood, focus, wallpaper and
 * sound apply, a hold is the server's (nothing to play), and more than 3
 * lines open with a line that they piled up while the user was away.
 */
export function planBacklog(items: InboxItem[], now: number): Play[] {
  const lastOutfit = items.reduce(
    (last, it, i) =>
      it.kind === 'outfit' && OUTFITS.includes(String(it.payload.outfit)) ? i : last,
    -1,
  );
  const lastOf = (kind: string) => items.reduce((last, it, i) => (it.kind === kind ? i : last), -1);
  const last = {
    mood: lastOf('mood'),
    focus: lastOf('focus'),
    wallpaper: lastOf('wallpaper'),
    sound: lastOf('sound'),
  };
  const fresh = (it: InboxItem) => now - Date.parse(it.created_at) <= ACT_FRESH_MS;
  const plan: Play[] = [];
  items.forEach((it, i) => {
    const p = it.payload;
    if (it.kind === 'say' || it.kind === 'event' || it.kind === 'remind') {
      if (it.say) plan.push({ kind: 'say', text: it.say });
    } else if (it.kind === 'ask') {
      const options = Array.isArray(p.options) ? p.options.map(String) : [];
      if (it.say && options.length > 0)
        plan.push({ kind: 'ask', id: it.id, text: it.say, options });
    } else if (it.kind === 'mood') {
      const level = Number(p.level);
      if (i === last.mood && fresh(it) && (level === -1 || level === 0 || level === 1))
        plan.push({ kind: 'mood', level, seconds: Number(p.seconds) || 600 });
    } else if (it.kind === 'card') {
      const title = str(p.title);
      if (title && fresh(it))
        plan.push({
          kind: 'card',
          title,
          text: str(p.text),
          url: str(p.url),
          seconds: Number(p.seconds) || 20,
        });
    } else if (it.kind === 'focus') {
      if (i === last.focus && fresh(it) && (p.action === 'start' || p.action === 'stop'))
        plan.push({
          kind: 'focus',
          action: p.action,
          minutes: typeof p.minutes === 'number' ? p.minutes : undefined,
        });
    } else if (it.kind === 'wallpaper' || it.kind === 'sound') {
      if (i === last[it.kind] && fresh(it)) plan.push({ kind: it.kind, which: str(p.which) });
    } else if (it.kind === 'outfit') {
      if (i === lastOutfit) plan.push({ kind: 'outfit', outfit: it.payload.outfit as AniOutfit });
    } else if (it.kind === 'act') {
      const name = it.payload.name;
      if (typeof name === 'string' && fresh(it)) plan.push({ kind: 'act', name });
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

/**
 * During a focus session Ani is off the page, but a focus item (start or
 * stop) still reaches it: only those are claimed, and the newest applies.
 */
export function useFocusInbox(
  enabled: boolean,
  onFocus: (action: 'start' | 'stop', minutes?: number) => void,
) {
  const focusRef = useRef(onFocus);
  focusRef.current = onFocus;
  useEffect(() => {
    if (!enabled) return;
    let gone = false;
    const tick = async () => {
      if (gone || document.visibilityState !== 'visible') return;
      try {
        const r = await api.postJson<{ data: InboxItem[] }>('/api/v1/ani/inbox/claim?kinds=focus');
        if (gone) return;
        const last = planBacklog(r.data, Date.now())
          .filter((p) => p.kind === 'focus')
          .pop();
        if (last?.kind === 'focus') focusRef.current(last.action, last.minutes);
      } catch {
        // The next tick tries again.
      }
    };
    void tick();
    const id = window.setInterval(() => void tick(), POLL_MS);
    return () => {
      gone = true;
      clearInterval(id);
    };
  }, [enabled]);
}
