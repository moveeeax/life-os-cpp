/*
 * Talking with Ani over the OpenAI Realtime API. The site's API mints a
 * short-lived client secret (the OpenAI key stays on the server, the model
 * and the voice are fixed there); the browser then talks to OpenAI over
 * WebRTC: the microphone goes up, her voice comes down as an audio track,
 * and a data channel carries the events: the text of what she says, and her
 * tool calls, which run here against the site's API as the signed-in user.
 * (developers.openai.com/api/docs/guides/realtime-webrtc)
 */

import { api } from '@/lib/api/client';

export type TalkState = 'connecting' | 'listening' | 'hearing' | 'thinking' | 'speaking';

/** A press shorter than this is a tap: nothing was said, so nothing is sent. */
export const HOLD_MIN_MS = 300;

/** Whether a hold from @p pressedAt to @p releasedAt carried speech worth sending. */
export function heldLongEnough(pressedAt: number, releasedAt: number): boolean {
  return releasedAt - pressedAt >= HOLD_MIN_MS;
}

export interface Tool {
  name: string;
  description: string;
  parameters: Record<string, unknown>;
  run(args: Record<string, unknown>): Promise<unknown>;
}

export interface TalkOptions {
  /** Who she is and what the page shows now. */
  instructions: string;
  tools: Tool[];
  /** Her voice, as it plays: for the lips. */
  onAudio(stream: MediaStream): void;
  /** What she is saying so far in this reply. */
  onText(text: string): void;
  onState(s: TalkState): void;
  /** The call ended on its own (silence, or the connection dropped). */
  onEnd(): void;
  /**
   * Hold to talk: the mic is open only while press() holds it and release()
   * sends what was said; without it the server hears the end of a phrase.
   */
  hold?: boolean;
  /**
   * Hanging up while it connects: the steps still ahead (the microphone,
   * the paid session key, the offer, her first reply) do not run, and no
   * callback fires after it.
   */
  signal?: AbortSignal;
}

/** What startTalk throws when its signal aborted the call before it was up. */
export class TalkAborted extends Error {
  constructor() {
    super('the call was hung up while it connected');
    this.name = 'TalkAborted';
  }
}

export interface Talk {
  /** Cuts her off mid-sentence (a tap on her while she talks). */
  interrupt(): void;
  stop(): void;
  /** Hold to talk: the mic opens. */
  press(): void;
  /** Hold to talk: the mic closes and she answers what was said. */
  release(): void;
}

// The data channel sometimes never opens on a call that did connect; such a
// call is dropped after OPEN_WAIT ms and placed again, up to TRIES times.
const OPEN_WAIT = 7000;
const TRIES = 3;
// Every minute of a call is paid for: after this long with nobody speaking,
// she hangs up.
const QUIET_MS = 90_000;
// A tool answer longer than this is cut (the model reads it all as input).
const TOOL_MAX = 6000;
// The microphone stays off while her voice plays and this long after the
// server has sent the last of it (what the browser still has buffered):
// through speakers it would hear her and answer her as the user.
const TAIL_MS = 600;

export async function startTalk(opts: TalkOptions): Promise<Talk> {
  let pc: RTCPeerConnection | null = null;
  let mic: MediaStream | null = null;
  let stopped = false;
  let holding = false;
  let pressedAt = 0;
  let listenRef: (on: boolean) => void = () => {};
  let quiet = 0;
  let tail = 0;
  // Sends a client event on the current call.
  let send: (ev: object) => void = () => {};
  const audio = new Audio();
  audio.autoplay = true;
  const hangUp = () => {
    pc?.close();
    pc = null;
  };
  const stop = () => {
    stopped = true;
    clearInterval(quiet);
    clearTimeout(tail);
    hangUp();
    for (const t of mic?.getTracks() ?? []) t.stop();
    mic = null;
    audio.srcObject = null;
  };
  // Nothing reaches the page once the call is stopped or hung up.
  const o: TalkOptions = {
    ...opts,
    onAudio: (s) => !stopped && opts.onAudio(s),
    onText: (t) => !stopped && opts.onText(t),
    onState: (st) => !stopped && opts.onState(st),
    onEnd: () => opts.onEnd(),
  };
  const end = () => {
    if (stopped) return;
    stop();
    o.onEnd();
  };
  const signal = opts.signal;
  if (signal?.aborted) throw new TalkAborted();
  signal?.addEventListener('abort', stop, { once: true });
  // Between the steps of connecting: a hang-up stops here.
  const check = () => {
    if (stopped) throw new TalkAborted();
  };

  // One call: true once the data channel is open.
  const call = async () => {
    const { value: key } = (
      await api.postJson<{ data: { value: string } }>('/api/v1/now/voice-session')
    ).data;
    check();

    const p = new RTCPeerConnection();
    pc = p;
    p.onconnectionstatechange = () => {
      if (p === pc && (p.connectionState === 'failed' || p.connectionState === 'closed')) end();
    };
    p.ontrack = (e) => {
      audio.srcObject = e.streams[0];
      o.onAudio(e.streams[0]);
    };
    p.addTrack(mic!.getTracks()[0], mic!);

    const dc = p.createDataChannel('oai-events');
    send = (ev: object) => {
      if (dc.readyState === 'open') dc.send(JSON.stringify(ev));
    };
    let text = '';
    let heard = Date.now();
    // From her reply's start until its audio has played out.
    let speaking = false;
    let audible = false;
    const listen = (on: boolean) => {
      // Holding to talk, only press() opens the mic.
      const open = on && (!o.hold || holding);
      for (const t of mic?.getAudioTracks() ?? []) t.enabled = open;
    };
    listenRef = listen;
    const quietDown = (wait: number) => {
      audible = false;
      clearTimeout(tail);
      tail = window.setTimeout(() => {
        if (audible || stopped) return;
        listen(true);
        if (!speaking) o.onState('listening');
        heard = Date.now();
      }, wait);
    };
    const runTool = async (callId: string, name: string, raw: string) => {
      const tool = o.tools.find((t) => t.name === name);
      let output: string;
      try {
        const args = raw ? (JSON.parse(raw) as Record<string, unknown>) : {};
        if (!tool) throw new Error(`no tool ${name}`);
        output = JSON.stringify(await tool.run(args));
      } catch (e) {
        output = JSON.stringify({ error: e instanceof Error ? e.message : String(e) });
      }
      if (output.length > TOOL_MAX) output = `${output.slice(0, TOOL_MAX)}…`;
      send({
        type: 'conversation.item.create',
        item: { type: 'function_call_output', call_id: callId, output },
      });
      send({ type: 'response.create' });
    };
    dc.onmessage = (m) => {
      const ev = JSON.parse(m.data as string) as {
        type: string;
        delta?: string;
        call_id?: string;
        name?: string;
        arguments?: string;
      };
      switch (ev.type) {
        case 'input_audio_buffer.speech_started':
          heard = Date.now();
          o.onState('hearing');
          break;
        case 'input_audio_buffer.speech_stopped':
          heard = Date.now();
          o.onState('thinking');
          break;
        case 'response.created':
          text = '';
          speaking = true;
          break;
        case 'response.output_audio_transcript.delta':
          text += ev.delta ?? '';
          o.onText(text);
          o.onState('speaking');
          break;
        case 'response.function_call_arguments.done':
          o.onState('thinking');
          void runTool(ev.call_id ?? '', ev.name ?? '', ev.arguments ?? '');
          break;
        case 'response.done':
          speaking = false;
          heard = Date.now();
          if (!audible) o.onState('listening');
          break;
        // Her voice as the server streams it (WebRTC only): the reply text
        // and response.done run ahead of what is heard.
        case 'output_audio_buffer.started':
          audible = true;
          clearTimeout(tail);
          listen(false);
          o.onState('speaking');
          break;
        case 'output_audio_buffer.stopped':
          quietDown(TAIL_MS);
          break;
        case 'output_audio_buffer.cleared':
          quietDown(0);
          break;
      }
    };
    const opened = new Promise<boolean>((done) => {
      const timer = setTimeout(() => done(false), OPEN_WAIT);
      dc.onopen = () => {
        clearTimeout(timer);
        done(true);
      };
    });

    const offer = await p.createOffer();
    check();
    await p.setLocalDescription(offer);
    const sdp = await fetch('https://api.openai.com/v1/realtime/calls', {
      method: 'POST',
      body: offer.sdp,
      headers: { Authorization: `Bearer ${key}`, 'Content-Type': 'application/sdp' },
      signal,
    });
    check();
    if (!sdp.ok) throw new Error(`calls ${sdp.status}`);
    await p.setRemoteDescription({ type: 'answer', sdp: await sdp.text() });
    check();
    const open = await opened;
    // Hung up while the channel opened: no session, no paid first reply.
    check();
    if (!open) return false;

    send({
      type: 'session.update',
      session: {
        type: 'realtime',
        instructions: o.instructions,
        tools: o.tools.map((t) => ({
          type: 'function',
          name: t.name,
          description: t.description,
          parameters: t.parameters,
        })),
        tool_choice: 'auto',
        // Holding to talk the page says when a phrase ends.
        ...(o.hold ? { audio: { input: { turn_detection: null } } } : {}),
      },
    });
    o.onState('listening');
    // She opens the conversation.
    send({ type: 'response.create' });
    quiet = window.setInterval(() => {
      if (!speaking && !audible && Date.now() - heard > QUIET_MS) end();
    }, 5000);
    return true;
  };

  try {
    o.onState('connecting');
    const got = await navigator.mediaDevices.getUserMedia({
      audio: { echoCancellation: true, noiseSuppression: true },
    });
    if (stopped) {
      // Hung up while the browser asked for the microphone: it goes off at once.
      for (const t of got.getTracks()) t.stop();
      throw new TalkAborted();
    }
    mic = got;
    for (let i = 1; ; i++) {
      if (await call()) break;
      hangUp();
      check();
      if (i >= TRIES) throw new Error('the call did not open');
    }
  } catch (e) {
    stop();
    throw signal?.aborted ? new TalkAborted() : e;
  } finally {
    signal?.removeEventListener('abort', stop);
  }
  return {
    interrupt: () => {
      send({ type: 'response.cancel' });
      send({ type: 'output_audio_buffer.clear' });
    },
    stop,
    press: () => {
      if (!o.hold || holding) return;
      holding = true;
      pressedAt = Date.now();
      // Talking over her cuts her off, as on a phone.
      send({ type: 'response.cancel' });
      send({ type: 'output_audio_buffer.clear' });
      send({ type: 'input_audio_buffer.clear' });
      listenRef(true);
      o.onState('listening');
    },
    release: () => {
      if (!o.hold || !holding) return;
      holding = false;
      listenRef(false);
      // A quick tap has an empty buffer, and Realtime answers an empty commit with an error.
      if (!heldLongEnough(pressedAt, Date.now())) {
        send({ type: 'input_audio_buffer.clear' });
        return;
      }
      send({ type: 'input_audio_buffer.commit' });
      send({ type: 'response.create' });
    },
  };
}
