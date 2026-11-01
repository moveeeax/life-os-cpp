import { useSyncExternalStore } from 'react';

import { ApiClientError, api } from '@/lib/api/client';
import type { components } from '@/lib/api/schema.gen';

/*
 * Writing to Ani's agent: an OpenClaw pod of the user's own, run by the
 * site's worker. A message queues a turn; the worker wakes the agent if it
 * sleeps (about half a minute), lets it read the user's Life OS through
 * tools, and stores the reply, which the panel polls for. A change the
 * agent wants to make (a task, a habit tick, a note) comes as a proposal
 * under the reply, and only the user's Confirm makes it.
 */

export type Turn = components['schemas']['AniTurn'];
export type Action = components['schemas']['AniAction'];

/** How often an open turn is read again at first. */
export const POLL_MS = 1000;
/** For how long the first, quick reads go on; a turn usually ends within it. */
export const POLL_FAST_FOR_MS = 10_000;
/** How often a turn still open after that is read (a sleeping agent wakes in about 30 s). */
export const POLL_SLOW_MS = 5000;

/** The wait before the next read of a turn watched for @p elapsed ms. */
export function pollDelay(elapsed: number): number {
  return elapsed < POLL_FAST_FOR_MS ? POLL_MS : POLL_SLOW_MS;
}

/**
 * Read a turn again and again: every second for ten seconds, then every five.
 * One read at a time (the next waits for the last to settle). While the tab is
 * hidden nothing is read; showing it again reads at once. Returns the stop.
 */
export function pollTurn(read: () => Promise<unknown>): () => void {
  const started = Date.now();
  let timer: number | undefined;
  let stopped = false;
  let reading = false;
  const hidden = () => typeof document !== 'undefined' && document.visibilityState === 'hidden';
  const schedule = () => {
    if (stopped) return;
    window.clearTimeout(timer);
    timer = window.setTimeout(tick, pollDelay(Date.now() - started));
  };
  const tick = () => {
    timer = undefined;
    if (stopped || reading || hidden()) return;
    reading = true;
    read()
      .catch(() => {})
      .finally(() => {
        reading = false;
        schedule();
      });
  };
  const onVisible = () => {
    if (!hidden() && timer === undefined && !reading) tick();
  };
  document.addEventListener('visibilitychange', onVisible);
  schedule();
  return () => {
    stopped = true;
    window.clearTimeout(timer);
    document.removeEventListener('visibilitychange', onVisible);
  };
}
/** The longest message, in characters (the API checks the same). */
export const TEXT_MAX = 4000;

export function isOpen(t: Turn): boolean {
  return t.status === 'queued' || t.status === 'running';
}

/** Why a turn failed, as the panel says it. */
export function failure(code: string | null): string {
  switch (code) {
    case 'busy':
      return 'Every place for an agent is taken. Try again in a few minutes.';
    case 'budget':
      return "This month's limit for the agent is spent.";
    case 'timeout':
      return 'Her agent did not wake up in time. Try again.';
    case 'unavailable':
      return 'Her agent did not answer. Try again.';
    default:
      return 'Something went wrong. Try again.';
  }
}

/** Why a message was not taken. */
export function refusal(e: unknown): string {
  if (e instanceof ApiClientError) {
    if (e.code === 'turn_open') return 'She is still answering the last message.';
    if (e.code === 'busy' || e.code === 'budget') return failure(e.code);
    if (e.status === 429) return 'That is a lot of messages for one hour. Try again later.';
    if (e.code === 'invalid_text') return `A message is up to ${TEXT_MAX} characters.`;
  }
  return 'The message was not sent. Try again.';
}

/** The chat so far, oldest first. */
export async function loadTurns(): Promise<Turn[]> {
  const res = await api.getJson('/api/v1/ani/turns', { query: { limit: 30 } });
  return [...res.data].reverse();
}

export async function sendMessage(text: string, tz: string): Promise<Turn> {
  return (await api.postJson<{ data: Turn }>('/api/v1/ani/messages', { body: { text, tz } })).data;
}

export async function readTurn(id: string): Promise<Turn> {
  return (await api.getJson<{ data: Turn }>(`/api/v1/ani/turns/${encodeURIComponent(id)}`)).data;
}

/** How often, and how many times, a confirmed change is read until the worker has made it. */
export const SETTLE_MS = 2000;
export const SETTLE_TRIES = 15;

/**
 * Reads @p turnId until its action @p actionId is no longer being made
 * (confirmed or running); the action as it ended, or null when it did not
 * end within SETTLE_TRIES reads or the reads failed.
 */
export async function settledAction(
  turnId: string,
  actionId: string,
  wait: (ms: number) => Promise<void> = (ms) => new Promise((ok) => setTimeout(ok, ms)),
): Promise<Action | null> {
  for (let i = 0; i < SETTLE_TRIES; i++) {
    await wait(SETTLE_MS);
    try {
      const a = (await readTurn(turnId)).actions.find((x) => x.id === actionId);
      if (!a) return null;
      if (a.status !== 'confirmed' && a.status !== 'running') return a;
    } catch {
      return null;
    }
  }
  return null;
}

/** The reply of a done turn read aloud (MP3); `engine.say` plays it with her lips. */
export function speechUrl(id: string): string {
  return `/api/v1/ani/turns/${encodeURIComponent(id)}/speech`;
}

// Kept under the chat's old key: the choice made there still holds.
const SOUND_KEY = 'now:ani:chat:sound';
let sound: boolean | null = null;
const soundListeners = new Set<() => void>();

/**
 * Whether she speaks aloud (her chat replies, her answers to taps, her
 * hellos); on unless the user turned it off. Off, her lines are only shown.
 */
export function soundOn(): boolean {
  if (sound === null) {
    try {
      sound = localStorage.getItem(SOUND_KEY) !== 'off';
    } catch {
      sound = true;
    }
  }
  return sound;
}

export function setSoundOn(on: boolean): void {
  sound = on;
  try {
    localStorage.setItem(SOUND_KEY, on ? 'on' : 'off');
  } catch {
    // Private mode: the choice lasts for this page.
  }
  for (const l of soundListeners) l();
}

/** The sound switch, the same in the chat and on her ledge. */
export function useSound(): [boolean, (on: boolean) => void] {
  const on = useSyncExternalStore(
    (l) => {
      soundListeners.add(l);
      return () => soundListeners.delete(l);
    },
    soundOn,
    () => true,
  );
  return [on, setSoundOn];
}

/** Deletes one message and her reply from the chat (her agent still remembers it). */
export async function deleteTurn(id: string): Promise<void> {
  await api.deleteJson(`/api/v1/ani/turns/${encodeURIComponent(id)}`);
}

/** Clears the chat; her agent starts a new conversation. */
export async function clearChat(): Promise<void> {
  await api.deleteJson('/api/v1/ani/turns');
}

/** Starts her agent while the user types; a refusal here shows up on send. */
export function wakeAgent(): void {
  void api.POST('/api/v1/ani/agent/wake').catch(() => {});
}

/** Whether a confirmed change of the turn is still being made. */
export function isDoing(t: Turn): boolean {
  return t.actions.some((a) => a.status === 'confirmed' || a.status === 'running');
}

/** What became of a proposal, as the panel says it; null while it waits for the user. */
export function actionState(a: Action): string | null {
  switch (a.status) {
    case 'pending':
      return null;
    case 'confirmed':
    case 'running':
      return 'Doing it';
    case 'done':
      return 'Done';
    case 'cancelled':
      return 'Cancelled';
    case 'expired':
      return 'Not confirmed in time';
    default:
      return 'Did not work';
  }
}

/** Confirms a proposal; a Telegram message may go with the person's own @p text. */
export async function confirmAction(id: string, text?: string): Promise<Action> {
  const path = `/api/v1/ani/actions/${encodeURIComponent(id)}/confirm`;
  return (
    text === undefined
      ? await api.postJson<{ data: Action }>(path)
      : await api.postJson<{ data: Action }>(path, { body: { text } })
  ).data;
}

/** Whether the person may change the words of a proposal before Confirm (a Telegram message). */
export function editable(a: Action): boolean {
  return (a.kind === 'tg_send' || a.kind === 'tg_edit') && a.status === 'pending' && a.text != null;
}

export async function cancelAction(id: string): Promise<Action> {
  return (
    await api.postJson<{ data: Action }>(`/api/v1/ani/actions/${encodeURIComponent(id)}/cancel`)
  ).data;
}

/** How long a question from the voice waits for her agent (a cold start is about a minute). */
export const ASK_WAIT_MS = 150_000;

/**
 * A question from the voice call to her agent: sent as a chat message, then
 * read until the reply. What comes back is what the voice says; the turn
 * itself stays in the chat, with any change it proposes waiting there.
 */
export async function askAgent(
  question: string,
  tz: string,
  wait = (ms: number) => new Promise((r) => setTimeout(r, ms)),
): Promise<Turn> {
  let t = await sendMessage(question, tz);
  for (let waited = 0; isOpen(t) && waited < ASK_WAIT_MS; waited += POLL_MS) {
    await wait(POLL_MS);
    t = await readTurn(t.id);
  }
  return t;
}

/** The chat a Telegram proposal is for, from its summary ("Send to Анна: «…»"); null for other kinds. */
export function chatOf(summary: string): string | null {
  const m = /^Send to (.+?): «/.exec(summary) ?? /^Edit your message in (.+?) to «/.exec(summary);
  return m ? m[1] : null;
}

/**
 * What the person confirms, in words. The summary of a Telegram proposal cuts
 * its message at 300 characters, while the whole text is what gets sent, so
 * that one shows the summary's head ("Send to Анна: ") with the whole text.
 */
export function confirmWords(a: Pick<Action, 'summary' | 'text'>): string {
  if (a.text == null) return a.summary;
  const at = a.summary.indexOf('«' + Array.from(a.text).slice(0, 20).join(''));
  return at >= 0 ? `${a.summary.slice(0, at)}«${a.text}»` : `${a.summary} ${a.text}`;
}
