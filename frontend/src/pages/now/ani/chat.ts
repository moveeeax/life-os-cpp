import { ApiClientError, api } from '@/lib/api/client';
import type { components } from '@/lib/api/schema.gen';

/*
 * Writing to Ani's agent: an OpenClaw pod of the user's own, run by the
 * site's worker. A message queues a turn; the worker wakes the agent if it
 * sleeps (about half a minute), lets it read the user's Life OS through
 * read-only tools, and stores the reply, which the panel polls for.
 */

export type Turn = components['schemas']['AniTurn'];

/** How often an open turn is read again. */
export const POLL_MS = 1000;
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

/** Starts her agent while the user types; a refusal here shows up on send. */
export function wakeAgent(): void {
  void api.POST('/api/v1/ani/agent/wake').catch(() => {});
}
