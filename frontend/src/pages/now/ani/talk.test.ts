// @vitest-environment jsdom
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { HOLD_MIN_MS, heldLongEnough, startTalk, type TalkState } from './talk';

vi.mock('@/lib/api/client', () => ({
  api: { postJson: vi.fn(() => Promise.resolve({ data: { value: 'ek_test' } })) },
}));

// A call that connects at once: the data channel opens, events come in
// through `server`, what the page sends lands in `sent`.
const track = { enabled: true, stop: vi.fn() };
let channel: {
  readyState: string;
  onopen?: () => void;
  onmessage?: (m: { data: string }) => void;
  send: (s: string) => void;
};
let sent: { type: string }[];
const server = (type: string) => channel.onmessage?.({ data: JSON.stringify({ type }) });

class FakePeer {
  connectionState = 'new';
  onconnectionstatechange?: () => void;
  ontrack?: () => void;
  addTrack() {}
  createDataChannel() {
    channel = {
      readyState: 'connecting',
      send: (s) => sent.push(JSON.parse(s) as { type: string }),
    };
    return channel;
  }
  createOffer() {
    return Promise.resolve({ sdp: 'offer' });
  }
  setLocalDescription() {
    return Promise.resolve();
  }
  setRemoteDescription() {
    channel.readyState = 'open';
    setTimeout(() => channel.onopen?.(), 0);
    return Promise.resolve();
  }
  close() {}
}

async function call(states: TalkState[]) {
  const talk = startTalk({
    instructions: '',
    tools: [],
    onAudio: () => {},
    onText: () => {},
    onState: (s) => states.push(s),
    onEnd: () => {},
  });
  await vi.advanceTimersByTimeAsync(10);
  return talk;
}

beforeEach(() => {
  vi.useFakeTimers();
  sent = [];
  track.enabled = true;
  vi.stubGlobal('RTCPeerConnection', FakePeer);
  vi.stubGlobal('Audio', class {});
  vi.stubGlobal('fetch', () =>
    Promise.resolve({ ok: true, text: () => Promise.resolve('answer') }),
  );
  Object.defineProperty(navigator, 'mediaDevices', {
    configurable: true,
    value: {
      getUserMedia: () =>
        Promise.resolve({ getTracks: () => [track], getAudioTracks: () => [track] }),
    },
  });
});
afterEach(() => {
  vi.useRealTimers();
  vi.unstubAllGlobals();
});

describe('startTalk', () => {
  it('keeps the microphone off while her voice plays, and a little after', async () => {
    const states: TalkState[] = [];
    const talk = await call(states);
    server('response.created');
    server('output_audio_buffer.started');
    expect(track.enabled).toBe(false);
    // The reply is generated before it is heard: still speaking.
    server('response.done');
    expect(track.enabled).toBe(false);
    expect(states.at(-1)).toBe('speaking');
    server('output_audio_buffer.stopped');
    await vi.advanceTimersByTimeAsync(300);
    expect(track.enabled).toBe(false);
    await vi.advanceTimersByTimeAsync(400);
    expect(track.enabled).toBe(true);
    expect(states.at(-1)).toBe('listening');
    talk.stop();
  });

  it('cuts her off on interrupt and listens at once', async () => {
    const talk = await call([]);
    server('response.created');
    server('output_audio_buffer.started');
    talk.interrupt();
    expect(sent.map((e) => e.type).slice(-2)).toEqual([
      'response.cancel',
      'output_audio_buffer.clear',
    ]);
    server('output_audio_buffer.cleared');
    await vi.advanceTimersByTimeAsync(0);
    expect(track.enabled).toBe(true);
    talk.stop();
  });
});

describe('hold to talk', () => {
  it('HoldToTalkTapSendsNothing: a tap is not a question', () => {
    expect(heldLongEnough(1000, 1000 + HOLD_MIN_MS - 1)).toBe(false);
    expect(heldLongEnough(1000, 1000 + HOLD_MIN_MS)).toBe(true);
  });
});
