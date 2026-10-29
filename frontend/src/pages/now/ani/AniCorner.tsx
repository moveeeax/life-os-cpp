import { useEffect, useRef, useState } from 'react';

import { api } from '@/lib/api/client';

import { fetchAniBare, useAniPrefs, useMuteKind } from '@/hooks/useAni';
import type { NowWeather, Today } from '@/lib/now/types';

import { AniChat, SpeakerIcon } from './AniChat';
import { soundOn, speechUrl, useSound } from './chat';
import type { Ani } from './engine';
import { REVIEW_EVENT } from '../review/review';
import { evening, tomorrowOf, waitingWords, type Waiting } from './evening';
import { greeting, HELLO, hourOf } from './greet';
import { useInbox, type Play } from './inbox';
import { moodLevel, moodOf, moveMood } from './mood';
import type { Talk, TalkState } from './talk';
import { useAni, type AniOutfit } from './state';
import { createToucher, say, type Answer } from './touch';
import { AniOffers } from './AniOffers';
import { HOLD_KEY, eventOfLine, quietKind, snooze, type LineKind } from './lines';
import { RIBBON_POINT, type NowDay } from '../ribbon/layout';
import './ani.css';

function holdToTalk(): boolean {
  try {
    return localStorage.getItem(HOLD_KEY) === '1';
  } catch {
    return false;
  }
}

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

// Recorded ahead with the voice she talks in (OpenAI TTS, marin), under
// v2/voice/: the hellos of the hour (greet.ts), the taps' lines
// (touch.ts) and this one.
const DIZZY: Answer = { text: 'Ой, голова кружится.', recorded: 'dizzy' };

/** The hello of this hour, recorded. */
function hello(): Answer {
  const h = hourOf(new Date().getHours());
  return { text: HELLO[h], recorded: h };
}

// The day her first hello and her evening word were said in this browser
// (YYYY-MM-DD).
const HELLO_KEY = 'now:ani:hello';
const EVENING_KEY = 'now:ani:evening';
function dayOf(key: string): string | null {
  try {
    return localStorage.getItem(key);
  } catch {
    return null;
  }
}
function setDayOf(key: string, day: string) {
  try {
    localStorage.setItem(key, day);
  } catch {
    // Private mode: she may say it again on the next visit.
  }
}
const helloDay = () => dayOf(HELLO_KEY);
const setHelloDay = (day: string) => setDayOf(HELLO_KEY, day);

// An hour of activity on the page, with no pause of five minutes, earns a
// word about a break; at most one an hour. Her evening word comes after 21:00.
const STRETCH_MS = 60 * 60_000;
const PAUSE_MS = 5 * 60_000;
const EVENING_HOUR = 21;

/** A microphone: talk with her (OpenAI Realtime). */
function MicIcon() {
  return (
    <svg
      viewBox="0 0 24 24"
      width="16"
      height="16"
      aria-hidden="true"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.8"
      strokeLinecap="round"
      strokeLinejoin="round"
    >
      <rect x="9" y="3" width="6" height="11" rx="3" />
      <path d="M5.5 11a6.5 6.5 0 0 0 13 0M12 17.5V21" />
    </svg>
  );
}

/** A speech balloon: write to her agent. */
function ChatIcon() {
  return (
    <svg
      viewBox="0 0 24 24"
      width="16"
      height="16"
      aria-hidden="true"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.8"
      strokeLinecap="round"
      strokeLinejoin="round"
    >
      <path d="M20 12.5c0 3.6-3.6 6.5-8 6.5-1 0-2-.15-2.9-.43L4.5 20l1.2-3.3C4.6 15.5 4 14 4 12.5 4 8.9 7.6 6 12 6s8 2.9 8 6.5Z" />
    </svg>
  );
}

const TALK_WORDS: Record<TalkState, string> = {
  connecting: 'Connecting',
  listening: 'Listening',
  hearing: 'Hearing you',
  thinking: 'Thinking',
  speaking: 'Speaking',
};

// Cold while bare: from COLD_FROM to COLD_TO o'clock; the first shiver after
// COLD_FIRST_MS bare, the next COLD_EVERY_MS later, then she dresses.
const COLD_FROM = 21;
/** How long a question's buttons stay (the server keeps it open as long). */
const ASK_OPEN_MS = 15 * 60_000;
const COLD_TO = 6;
const COLD_FIRST_MS = 90_000;
const COLD_EVERY_MS = 4 * 60_000;

const OUTFITS: { id: AniOutfit; label: string }[] = [
  { id: 'p1', label: 'Dress' },
  { id: 'p2', label: 'Slip' },
  { id: 'lingerie', label: 'Lingerie' },
  { id: 'bare', label: 'Bare' },
];

/** What she knows when a conversation starts. */
export interface AniContext {
  name?: string;
  tz: string;
  date: string;
  today?: Today;
  weather?: NowWeather;
  /** The day ribbon's data: a line about an event points at it. */
  day?: NowDay;
}

/**
 * Ani in the bottom left corner of Now: the model standing on a glass ledge
 * left of the dock. The ledge sends her off through the left edge and calls
 * her back; its hanger opens the outfits, and a change happens off screen.
 * A drag on her turns her round; a tap answers by where it lands on her
 * (touch.ts), aloud; coming back she greets. Her first hello of the day
 * names a thing from the day (greet.ts); she cheers a ticked focus and sends
 * the user on a break after a focus session. The speaker on the ledge mutes
 * her (the chat's switch too); muted, her lines are only shown. The
 * microphone under the hanger starts a conversation in an AI voice
 * (OpenAI); the speech balloon next to the hanger opens a chat with her
 * agent (AniChat). The engine (three and the model, about 15 MB
 * from the bucket) loads only when she is on.
 */
export function AniCorner({
  base,
  context,
  afterFocus = false,
  onAfterFocus,
  onFocus,
  onWallpaper,
  onSound,
  onTasksMoved,
}: {
  base: string;
  context: AniContext;
  /** She comes back after a focus session that ran to its end. */
  afterFocus?: boolean;
  /** Her word about a break is said (once). */
  onAfterFocus?: () => void;
  /** Her webhook or MCP starts or stops the focus timer. */
  onFocus?: (action: 'start' | 'stop', minutes?: number) => void;
  /** ... switches the wallpaper (an id; none: the next one). */
  onWallpaper?: (which?: string) => void;
  /** ... switches the focus sound (an id, or off). */
  onSound?: (which?: string) => void;
  /** Her evening offer moved tasks: the page loads its day again. */
  onTasksMoved?: () => void;
}) {
  const [ani, setAni] = useAni();
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const hitRef = useRef<HTMLDivElement>(null);
  const engine = useRef<Ani | null>(null);
  const [ready, setReady] = useState(false);
  const [failed, setFailed] = useState(false);
  const [busy, setBusy] = useState<'leaving' | 'arriving' | 'changing' | null>(null);
  const [wardrobe, setWardrobe] = useState(false);
  const [chat, setChat] = useState(false);
  const cornerRef = useRef<HTMLDivElement>(null);
  // Bumped when a voice question reaches her agent: the chat opens, or loads again.
  const [chatKey, setChatKey] = useState(0);
  const ledgeRef = useRef<HTMLDivElement>(null);
  // The first state the engine starts from; later changes go through calls.
  // Hide numbers (a shared screen): she is not shown bare, the lingerie
  // stands in until it is off again.
  const [hideNums, setHideNums] = useState(() =>
    document.documentElement.classList.contains('hide-numbers'),
  );
  useEffect(() => {
    const root = document.documentElement;
    const mo = new MutationObserver(() => setHideNums(root.classList.contains('hide-numbers')));
    mo.observe(root, { attributes: true, attributeFilter: ['class'] });
    return () => mo.disconnect();
  }, []);
  // Bare only while it is on in her settings (server side, 18+).
  const prefs = useAniPrefs();
  const bareOn = prefs.data?.bare === true;
  const bareHidden = hideNums || !bareOn;
  const shown: AniOutfit = ani.outfit === 'bare' && bareHidden ? 'lingerie' : ani.outfit;
  // A stored bare outfit waits for the settings, so she is not mounted twice.
  const waitPrefs = ani.outfit === 'bare' && prefs.isPending;
  const outfitRef = useRef(shown);
  outfitRef.current = shown;
  // Bare is another model: switching to or from it mounts her again, as she
  // is now (the engine starts from these).
  const body = shown === 'bare' ? 'bare' : 'dressed';
  const mountRef = useRef({ outfit: shown, here: ani.here });
  mountRef.current = { outfit: shown, here: ani.here };
  const [said, setSaid] = useState<string | null>(null);
  const [talk, setTalk] = useState<TalkState | null>(null);
  const talkRef = useRef<Talk | null>(null);
  // From the microphone tap until the call ends, connecting included: her
  // own lines keep quiet, the call's microphone would hear them.
  const calling = useRef(false);
  const stopTalk = () => {
    calling.current = false;
    talkRef.current?.stop();
    talkRef.current = null;
    engine.current?.follow(null);
    setTalk(null);
    setSaid(null);
  };
  const startTalking = async () => {
    const e = engine.current;
    if (!e) return;
    calling.current = true;
    e.hush();
    clearTimeout(sayTimer.current);
    setTalk('connecting');
    setSaid(null);
    const [{ startTalk }, persona] = await Promise.all([import('./talk'), import('./persona')]);
    try {
      const t = await startTalk({
        instructions: persona.instructions({ ...context, now: new Date() }),
        tools: persona.tools({
          ...context,
          // A change she proposed from the call waits for the user in the chat.
          onAgentTurn: (t) => {
            if (t.actions.some((a) => a.status === 'pending')) {
              setChatKey((k) => k + 1);
              setChat(true);
            }
          },
        }),
        onAudio: (stream) => e.follow(stream),
        onText: (t) => setSaid(t),
        onState: (st) => setTalk(st),
        onEnd: () => stopTalk(),
        hold: holdToTalk(),
      });
      // Hung up while it was connecting.
      if (!calling.current) t.stop();
      else talkRef.current = t;
    } catch {
      stopTalk();
      setSaid('Не получилось подключиться. Попробуй ещё раз чуть позже.');
    }
  };
  // Holding to talk, the space bar works as the mic button (outside text fields).
  useEffect(() => {
    if (!talk || !holdToTalk()) return;
    const typing = (e: KeyboardEvent) =>
      e.target instanceof HTMLElement && ['INPUT', 'TEXTAREA'].includes(e.target.tagName);
    const down = (e: KeyboardEvent) => {
      if (e.code !== 'Space' || e.repeat || typing(e)) return;
      e.preventDefault();
      talkRef.current?.press();
    };
    const up = (e: KeyboardEvent) => {
      if (e.code === 'Space' && !typing(e)) talkRef.current?.release();
    };
    addEventListener('keydown', down);
    addEventListener('keyup', up);
    return () => {
      removeEventListener('keydown', down);
      removeEventListener('keyup', up);
    };
  }, [talk]);
  useEffect(
    () => () => {
      calling.current = false;
      talkRef.current?.stop();
    },
    [],
  );
  // Her answers to taps on her body read the day as it is now.
  const contextRef = useRef(context);
  contextRef.current = context;
  const toucher = useRef(
    createToucher({
      ani: () => engine.current,
      today: () => contextRef.current.today,
      focusField: () => document.querySelector<HTMLInputElement>('.n-input'),
      hidden: () => document.documentElement.classList.contains('hide-numbers'),
      bare: () => outfitRef.current === 'bare',
    }),
  );
  const sayTimer = useRef(0);
  const lastSaidAt = useRef(0);
  // The bubble opens the chat (her evening word).
  const [saidChat, setSaidChat] = useState(false);
  // The kind of the line on screen ("Later", "Not about this") and the turn whose proposals show.
  const [saidKind, setSaidKind] = useState<LineKind | null>(null);
  const [offersTurn, setOffersTurn] = useState<string | null>(null);
  const mutedRef = useRef<string[]>([]);
  mutedRef.current = prefs.data?.muted ?? [];
  const muteKind = useMuteKind();
  /**
   * Her answer to a tap, in the bubble while she says it: a recorded line
   * from the bucket, one with the day in it read by the server, or a silent
   * one shown for a moment.
   */
  // Says a line; the promise ends when the bubble goes (a webhook backlog
  // plays one line after the other).
  const answer = useRef<(a: Answer) => Promise<void>>(() => Promise.resolve());
  // The promise of the line on screen: a line that cuts it short ends it.
  const lineEnds = useRef<(() => void) | null>(null);
  answer.current = (a: Answer) =>
    new Promise<void>((finished) => {
      const e = engine.current;
      if (!e || calling.current) return finished();
      // A kind the person muted or put off keeps quiet.
      if (quietKind(a.kind, mutedRef.current)) return finished();
      lineEnds.current?.();
      lineEnds.current = finished;
      if (a.mood) e.mood(moodLevel(moveMood(contextRef.current.date, a.mood)));
      lastSaidAt.current = Date.now();
      setSaidChat(Boolean(a.chat));
      setSaid(a.text);
      setSaidKind(a.kind ?? null);
      if (a.turn) setOffersTurn(a.turn);
      // A line about an event of the day lights it on the ribbon.
      const ev = eventOfLine(a.text, contextRef.current.day, contextRef.current.tz);
      if (ev) window.dispatchEvent(new CustomEvent(RIBBON_POINT, { detail: ev }));
      clearTimeout(sayTimer.current);
      const done = () => {
        setSaid((v) => (v === a.text ? null : v));
        if (lineEnds.current === finished) lineEnds.current = null;
        finished();
      };
      if (a.silent || !soundOn()) {
        sayTimer.current = window.setTimeout(done, a.silent ? 1500 : 1500 + a.text.length * 50);
        return;
      }
      const url = a.recorded
        ? `${base}v2/voice/${a.recorded}.mp3`
        : `/api/v1/now/voice-line?text=${encodeURIComponent(a.text)}`;
      // No voice (offline, the limit): the line still stays up for a while.
      const started = Date.now();
      void e.say(url).then(() => {
        const left = 1200 + a.text.length * 40 - (Date.now() - started);
        if (left > 0) sayTimer.current = window.setTimeout(done, left);
        else done();
      });
    });
  const [sound, setSound] = useSound();

  // Her mood of the day shows in her face from the start.
  useEffect(() => {
    if (ready) engine.current?.mood(moodLevel(moodOf(contextRef.current.date)));
  }, [ready]);

  // An hour at the screen and the evening: checked every half minute while
  // she is here, ready and not in a conversation, on a visible page.
  const hereRef = useRef(ani.here);
  hereRef.current = ani.here && ready;
  useEffect(() => {
    let active = Date.now();
    let streak = active;
    let stretched = active;
    const onActive = () => {
      const now = Date.now();
      if (now - active > PAUSE_MS) streak = now;
      active = now;
    };
    const events = ['pointermove', 'pointerdown', 'keydown', 'wheel'] as const;
    for (const ev of events) addEventListener(ev, onActive, { passive: true });
    const id = window.setInterval(() => {
      const now = Date.now();
      if (!hereRef.current || calling.current || document.visibilityState !== 'visible') return;
      // Not over another line.
      if (now - lastSaidAt.current < 60_000) return;
      const c = contextRef.current;
      if (
        new Date().getHours() >= EVENING_HOUR &&
        helloDay() === c.date &&
        dayOf(EVENING_KEY) !== c.date
      ) {
        setDayOf(EVENING_KEY, c.date);
        // Telegram answers still owed go after the day's line (none without Telegram).
        void api
          .getJson<{ data: Waiting[] }>('/api/v1/telegram/waiting')
          .then((r) => (Array.isArray(r.data) ? r.data : []))
          .catch(() => [] as Waiting[])
          .then((waiting) => {
            const owed = waitingWords(waiting, new Date());
            // The day's tasks first: done of all, and an offer to move the rest.
            const sum = evening(
              c.today,
              document.documentElement.classList.contains('hide-numbers'),
            );
            if (sum) {
              if (sum.move.length > 0) setMoveOffer({ ids: sum.move, due: tomorrowOf(c.date) });
              answer.current({ text: owed ? `${sum.text} ${owed}` : sum.text, kind: 'evening' });
              return;
            }
            if (owed) {
              answer.current({ text: owed, kind: 'telegram' });
              return;
            }
            const f = c.today?.focus;
            const line =
              f && !f.done ? say('focusOpen') : f?.done ? say('dayDone') : say('howWasDay');
            answer.current({ ...line, chat: true });
          });
        return;
      }
      if (now - active < 60_000 && now - streak >= STRETCH_MS && now - stretched >= STRETCH_MS) {
        stretched = now;
        streak = now;
        engine.current?.act('idle_happy');
        answer.current({ ...say('stretch'), kind: 'stretch' });
      }
    }, 30_000);
    return () => {
      clearInterval(id);
      for (const ev of events) removeEventListener(ev, onActive);
    };
  }, []);

  // Bare in the evening and at night she gets cold: after a while she hugs
  // herself and shivers, later once more, and then goes to dress (the
  // lingerie). Checked every 20 s while she is here and nothing else goes on.
  // Her outfit change, set once wear() is defined below.
  const wearRef = useRef<(o: AniOutfit) => void>(() => {});

  // Her webhook: items play while she may speak (here, no voice call, no
  // Hide numbers on); otherwise they wait on the server and a dot says so.
  // The weekly review opens: she says one line about it.
  useEffect(() => {
    // Not during a voice call, and only while she is here.
    const onReview = () => {
      if (hereRef.current && !calling.current) answer.current({ text: 'Давай глянем неделю.' });
    };
    window.addEventListener(REVIEW_EVENT, onReview);
    return () => window.removeEventListener(REVIEW_EVENT, onReview);
  }, []);
  // Her evening offer to move today's open tasks to tomorrow.
  const [moveOffer, setMoveOffer] = useState<{ ids: string[]; due: string } | null>(null);
  const moveTasks = (offer: { ids: string[]; due: string }) => {
    setMoveOffer(null);
    void Promise.all(
      offer.ids.map((id) =>
        api.patchJson(`/api/v1/tasks/items/${id}`, {
          body: { due: offer.due },
          query: { tz: context.tz },
        }),
      ),
    ).then(
      () => {
        pageRef.current.onTasksMoved?.();
        answer.current({ text: 'Перенесла на завтра.' });
      },
      () => answer.current({ text: 'Не получилось перенести, попробуй в задачах.' }),
    );
  };
  // A card over her ledge and a question with buttons, from the same inbox.
  const [card, setCard] = useState<{ title: string; text?: string; url?: string } | null>(null);
  // Her questions wait in line: one shows at a time, the next after an
  // answer or once it closes (the server keeps it open 15 minutes).
  const [asks, setAsks] = useState<{ id: string; options: string[]; until: number }[]>([]);
  const ask = asks[0] ?? null;
  useEffect(() => {
    if (!ask) return;
    const t = window.setTimeout(
      () => setAsks((q) => q.filter((a) => a.id !== ask.id)),
      Math.max(0, ask.until - Date.now()),
    );
    return () => window.clearTimeout(t);
  }, [ask]);
  const cardTimer = useRef(0);
  const moodTimer = useRef(0);
  const pageRef = useRef({ onFocus, onWallpaper, onSound, onTasksMoved });
  pageRef.current = { onFocus, onWallpaper, onSound, onTasksMoved };
  useEffect(
    () => () => {
      window.clearTimeout(cardTimer.current);
      window.clearTimeout(moodTimer.current);
    },
    [],
  );
  const sendAnswer = (id: string, text: string) => {
    setAsks((q) => q.filter((a) => a.id !== id));
    void api.postJson(`/api/v1/ani/inbox/${id}/answer`, { body: { answer: text } }).catch(() => {});
  };
  const { waiting } = useInbox(
    ready && !failed && ani.here && !hideNums && talk == null,
    (p: Play): Promise<void> => {
      const pause = (ms: number) => new Promise<void>((ok) => window.setTimeout(ok, ms));
      switch (p.kind) {
        case 'say':
          return answer.current({ text: p.text, kind: p.line, turn: p.turn });
        case 'ask':
          setAsks((q) =>
            q.some((a) => a.id === p.id)
              ? q
              : [...q, { id: p.id, options: p.options, until: Date.now() + ASK_OPEN_MS }],
          );
          return answer.current({ text: p.text });
        case 'act':
          engine.current?.act(p.name);
          return pause(4000);
        case 'outfit':
          wearRef.current(p.outfit);
          return pause(3000);
        case 'mood':
          engine.current?.mood(p.level);
          window.clearTimeout(moodTimer.current);
          moodTimer.current = window.setTimeout(() => engine.current?.mood(0), p.seconds * 1000);
          return pause(500);
        case 'card':
          setCard({ title: p.title, text: p.text, url: p.url });
          window.clearTimeout(cardTimer.current);
          cardTimer.current = window.setTimeout(() => setCard(null), p.seconds * 1000);
          return pause(1000);
        case 'focus':
          pageRef.current.onFocus?.(p.action, p.minutes);
          return pause(500);
        case 'wallpaper':
          pageRef.current.onWallpaper?.(p.which);
          return pause(500);
        case 'sound':
          pageRef.current.onSound?.(p.which);
          return pause(500);
      }
    },
  );
  const busyRef = useRef(busy);
  busyRef.current = busy;
  useEffect(() => {
    let since = 0;
    let shivers = 0;
    let last = 0;
    const id = window.setInterval(() => {
      const now = Date.now();
      const h = new Date().getHours();
      const cold = h >= COLD_FROM || h < COLD_TO;
      if (
        !cold ||
        outfitRef.current !== 'bare' ||
        !hereRef.current ||
        calling.current ||
        busyRef.current ||
        document.visibilityState !== 'visible'
      ) {
        since = 0;
        shivers = 0;
        return;
      }
      if (!since) since = now;
      if (now - since < COLD_FIRST_MS || now - last < COLD_EVERY_MS) return;
      if (now - lastSaidAt.current < 15_000) return;
      last = now;
      shivers += 1;
      const e = engine.current;
      e?.hug(5);
      e?.feel('shy', 3);
      if (shivers === 1) {
        answer.current({ text: 'Брр… прохладно.', mood: 0 });
        return;
      }
      answer.current({ text: 'Холодно. Пойду оденусь.', mood: 0 });
      since = 0;
      shivers = 0;
      window.setTimeout(() => wearRef.current('lingerie'), 4500);
    }, 20_000);
    return () => clearInterval(id);
  }, []);

  // Her first hello: once a day in this browser, with a thing from the day
  // (late tasks, rain, the night's sleep); after a focus session, a break.
  // Browsers play sound only after a gesture on the page: until one she waits.
  const hasToday = context.today != null;
  useEffect(() => {
    if (!ready || !ani.here || (!afterFocus && !hasToday)) return;
    let line: Answer;
    if (afterFocus) {
      line = { ...say('breakTime'), kind: 'break' };
      onAfterFocus?.();
      engine.current?.feel('happy', 3);
      engine.current?.act('peace');
    } else {
      if (helloDay() === contextRef.current.date) return;
      setHelloDay(contextRef.current.date);
      const c = contextRef.current;
      const hidden = document.documentElement.classList.contains('hide-numbers');
      line = {
        ...greeting(c.today, c.weather, new Date().getHours(), hidden),
        kind: new Date().getHours() < 12 ? 'brief' : 'hello',
      };
    }
    if (navigator.userActivation?.hasBeenActive !== false) {
      answer.current(line);
      return;
    }
    const go = () => answer.current(line);
    addEventListener('pointerdown', go, { once: true });
    return () => removeEventListener('pointerdown', go);
    // Once per mount: when she is ready and the day is in.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [ready, hasToday]);

  // A focus ticked while she watches: she cheers.
  const focus = context.today?.focus;
  const focusKey = focus ? `${focus.date}|${focus.kind}|${focus.ref_id ?? focus.text}` : null;
  const ticked = useRef<{ key: string | null; done: boolean }>({ key: null, done: false });
  useEffect(() => {
    const was = ticked.current;
    ticked.current = { key: focusKey, done: Boolean(focus?.done) };
    if (!ready || !ani.here || !focus?.done || was.key !== focusKey || was.done) return;
    engine.current?.feel('happy', 3);
    engine.current?.act('peace');
    answer.current(say('focusTicked'));
  }, [focusKey, focus?.done, ready, ani.here]);

  useEffect(() => {
    const canvas = canvasRef.current;
    const hit = hitRef.current;
    if (!canvas || !hit) return;
    if (waitPrefs) return;
    let gone = false;
    let made: Ani | null = null;
    // The bare model comes by short signed links; without them, the lingerie.
    const links =
      body === 'bare'
        ? fetchAniBare().then(
            (l) => ({ model: l.model_url, texture: l.texture_url }),
            () => null,
          )
        : Promise.resolve(undefined);
    void Promise.all([import('./engine'), links]).then(([{ mountAni }, bare]) => {
      if (gone) return;
      if (bare === null) {
        setAni({ outfit: 'lingerie' });
        return;
      }
      try {
        made = mountAni(canvas, {
          base,
          bare,
          hit,
          height: 340,
          // The middle of her ledge's top edge.
          feet: () => {
            const r = ledgeRef.current?.getBoundingClientRect();
            return r ? { x: r.left + r.width / 2, y: r.top } : { x: 110, y: innerHeight - 126 };
          },
          field: () => document.querySelector<HTMLInputElement>('.n-input'),
          // Her speech bubble rides by her head.
          onHead: (x, y) => {
            cornerRef.current?.style.setProperty('--ani-hx', `${Math.round(x)}px`);
            cornerRef.current?.style.setProperty('--ani-hy', `${Math.round(y)}px`);
          },
          outfit: mountRef.current.outfit,
          present: mountRef.current.here,
          onReady: () => setReady(true),
          onError: () => setFailed(true),
          // A tap answers by where it landed (touch.ts).
          onTap: (z, at) => {
            // On a call a tap cuts her off, so the user can speak.
            if (calling.current) {
              talkRef.current?.interrupt();
              return;
            }
            const a = toucher.current.tap(z, at);
            if (a) answer.current(a);
          },
          onSpin: () => answer.current(DIZZY),
          // The cursor resting on her body (bare): "eyes up".
          onDwell: (z, at) => {
            const a = toucher.current.stare(z, at);
            if (a) answer.current(a);
          },
          // A double tap opens the chat with her agent.
          onDoubleTap: () => {
            made?.hush();
            setSaid(null);
            setChat(true);
          },
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
      setReady(false);
    };
    // setAni is stable.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [base, body, waitPrefs]);

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
    if (!here) {
      stopTalk();
      setSaid(null);
    }
    engine.current.present(here, () => {
      setBusy(null);
      // Back: she says hello.
      if (here) {
        // Back in a good mood she missed the user; in a bad one, a "Хм".
        const level = moodLevel(moodOf(contextRef.current.date));
        answer.current(level === 1 ? say('missed') : level === -1 ? say('grumpyHello') : hello());
      }
    });
  };

  const wear = (o: AniOutfit) => {
    setWardrobe(false);
    const e = engine.current;
    if (busy || !e || o === ani.outfit) return;
    const next: AniOutfit = o === 'bare' && bareHidden ? 'lingerie' : o;
    if ((next === 'bare') !== (shown === 'bare')) {
      // The other model: she leaves, and comes back in it.
      const swap = () => {
        setAni({ outfit: o, here: true });
        setBusy(null);
      };
      if (!ani.here) swap();
      else {
        setBusy('changing');
        e.present(false, swap);
      }
      return;
    }
    setAni({ outfit: o });
    if (!ani.here) {
      // Away: she comes back already changed.
      e.setOutfit(next);
      setAni({ here: true });
      setBusy('arriving');
      e.present(true, () => setBusy(null));
      return;
    }
    setBusy('changing');
    e.present(false, () => {
      e.setOutfit(next);
      e.present(true, () => setBusy(null));
    });
  };
  wearRef.current = wear;

  const words = talk
    ? ['Talking', TALK_WORDS[talk]]
    : busy === 'changing'
      ? ['Changing', 'Back in a moment']
      : ani.here
        ? ['With you', 'Send her off']
        : ['Away', 'Call her back'];

  return (
    <div ref={cornerRef} className="ani-corner" data-ready={ready ? '' : undefined}>
      <canvas ref={canvasRef} className="ani-canvas" aria-hidden="true" />
      {said && ani.here && !chat ? (
        saidChat ? (
          <button
            type="button"
            className="ani-say ani-say-chat"
            data-num-ok
            title="Write to Ani"
            onClick={() => {
              engine.current?.hush();
              setSaid(null);
              setChat(true);
            }}
          >
            {said}
          </button>
        ) : (
          <div className="ani-say" role="status" data-num-ok>
            {said}
          </div>
        )
      ) : null}
      {said && ani.here && !chat && saidKind ? (
        <div className="ani-ask ani-kind" role="group" aria-label="About this kind of line">
          <button
            type="button"
            onClick={() => {
              snooze(saidKind);
              engine.current?.hush();
              setSaid(null);
            }}
          >
            Потом
          </button>
          <button
            type="button"
            onClick={() => {
              muteKind(saidKind);
              mutedRef.current = [...new Set([...mutedRef.current, saidKind])];
              engine.current?.hush();
              setSaid(null);
            }}
          >
            Не про это
          </button>
        </div>
      ) : null}
      {offersTurn && ani.here && !chat ? (
        <AniOffers key={offersTurn} turn={offersTurn} onDone={() => setOffersTurn(null)} />
      ) : null}
      <div
        ref={hitRef}
        className="ani-hit"
        data-away={ani.here ? undefined : ''}
        role="img"
        aria-label="Ani. Drag to turn her, tap her to see what she says."
      />
      {card && !hideNums ? (
        <div className="ani-card num" role="note" aria-label={card.title}>
          <div className="ani-card-title">{card.title}</div>
          {card.text ? <div className="ani-card-text">{card.text}</div> : null}
          {card.url ? (
            <a href={card.url} target="_blank" rel="noopener noreferrer">
              Open
            </a>
          ) : null}
        </div>
      ) : null}
      {moveOffer && !ask ? (
        <div className="ani-ask" role="group" aria-label="Move to tomorrow">
          <button type="button" onClick={() => moveTasks(moveOffer)}>
            Перенести
          </button>
          <button type="button" onClick={() => setMoveOffer(null)}>
            Нет
          </button>
        </div>
      ) : null}
      {ask && !hideNums ? (
        <div className="ani-ask" role="group" aria-label="Her question">
          {ask.options.map((o) => (
            <button key={o} type="button" className="num" onClick={() => sendAnswer(ask.id, o)}>
              {o}
            </button>
          ))}
        </div>
      ) : null}
      <div ref={ledgeRef} className="ani-ledge" data-wardrobe={wardrobe ? '' : undefined}>
        {waiting > 0 ? (
          <span
            className="ani-inbox"
            role="status"
            aria-label={`${waiting} waiting from your webhook`}
            title="Something came through your webhook"
          />
        ) : null}
        {wardrobe ? (
          <div className="ani-wardrobe" role="group" aria-label="Outfit">
            <span className="ani-k">Outfit</span>
            {OUTFITS.filter((o) => !(bareHidden && o.id === 'bare')).map((o) => (
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
              Animation credits to pixiv Inc.&apos;s VRoid Project, Pic♡Pix and Mixamo. Her voice is
              AI-generated (OpenAI).
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
        {wardrobe ? null : (
          <button
            type="button"
            className="ani-chat-btn"
            aria-label={chat ? 'Close the chat' : 'Write to Ani'}
            title="Write to Ani (an AI agent, OpenAI)"
            aria-expanded={chat}
            onClick={() => setChat((v) => !v)}
          >
            <ChatIcon />
          </button>
        )}
        {wardrobe ? null : (
          <button
            type="button"
            className="ani-mic"
            aria-label={talk ? 'Stop talking' : 'Talk with Ani'}
            title={talk ? 'Stop talking' : 'Talk with Ani (AI-generated voice, OpenAI)'}
            aria-pressed={talk != null}
            data-state={talk ?? undefined}
            disabled={!ready || !ani.here}
            onClick={() => (talk ? stopTalk() : void startTalking())}
            // Holding to talk, a press on a call opens the mic and letting go sends.
            onPointerDown={() => talk && holdToTalk() && talkRef.current?.press()}
            onPointerUp={() => talkRef.current?.release()}
            onPointerLeave={() => talkRef.current?.release()}
          >
            <MicIcon />
          </button>
        )}
        {wardrobe ? null : (
          <button
            type="button"
            className="ani-sound"
            aria-pressed={sound}
            aria-label={sound ? 'Ani speaks aloud: mute her' : 'Ani is muted: let her speak'}
            title={sound ? 'Mute Ani' : 'Let Ani speak'}
            onClick={() => setSound(!sound)}
          >
            <SpeakerIcon on={sound} />
          </button>
        )}
      </div>
      {chat ? (
        <AniChat
          key={chatKey}
          tz={context.tz}
          onClose={() => setChat(false)}
          onReply={(t) => {
            // She speaks only when she is on screen and not in a voice call.
            if (engine.current && ani.here && !calling.current) {
              void engine.current.say(speechUrl(t.id));
            }
          }}
        />
      ) : null}
    </div>
  );
}
