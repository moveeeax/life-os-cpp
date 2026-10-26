import { useEffect, useRef, useState } from 'react';

import type { Today } from '@/lib/now/types';

import { AniChat } from './AniChat';
import { speechUrl } from './chat';
import type { Ani } from './engine';
import type { Talk, TalkState } from './talk';
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

// Lines recorded ahead with the voice she talks in (OpenAI TTS, marin),
// under v2/voice/<line>.mp3.
const LINES = {
  morning: 'Доброе утро! Я уже тут.',
  day: 'Привет! Как идут дела?',
  evening: 'Добрый вечер. Долгий был день?',
  night: 'Уже поздно. Может, пора спать?',
  tickle: 'Эй, щекотно!',
  dizzy: 'Ой, голова кружится.',
} as const;
type Line = keyof typeof LINES;

function hello(): Line {
  const h = new Date().getHours();
  return h < 5 ? 'night' : h < 12 ? 'morning' : h < 18 ? 'day' : h < 23 ? 'evening' : 'night';
}

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

const OUTFITS: { id: AniOutfit; label: string }[] = [
  { id: 'p1', label: 'Dress' },
  { id: 'p2', label: 'Slip' },
  { id: 'lingerie', label: 'Lingerie' },
];

/** What she knows when a conversation starts. */
export interface AniContext {
  name?: string;
  tz: string;
  date: string;
  today?: Today;
}

/**
 * Ani in the bottom left corner of Now: the model standing on a glass ledge
 * left of the dock. The ledge sends her off through the left edge and calls
 * her back; its hanger opens the outfits, and a change happens off screen.
 * A drag on her turns her round, a tap says hello (then tickles); coming
 * back she greets. The microphone under the hanger starts a conversation in
 * an AI voice (OpenAI); the speech balloon next to the hanger opens a chat
 * with her agent (AniChat). The engine (three and the model, about 15 MB
 * from the bucket) loads only when she is on.
 */
export function AniCorner({ base, context }: { base: string; context: AniContext }) {
  const [ani, setAni] = useAni();
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const hitRef = useRef<HTMLDivElement>(null);
  const engine = useRef<Ani | null>(null);
  const [ready, setReady] = useState(false);
  const [failed, setFailed] = useState(false);
  const [busy, setBusy] = useState<'leaving' | 'arriving' | 'changing' | null>(null);
  const [wardrobe, setWardrobe] = useState(false);
  const [chat, setChat] = useState(false);
  // Bumped when a voice question reaches her agent: the chat opens, or loads again.
  const [chatKey, setChatKey] = useState(0);
  const ledgeRef = useRef<HTMLDivElement>(null);
  // The first state the engine starts from; later changes go through calls.
  const initial = useRef(ani);
  const [said, setSaid] = useState<string | null>(null);
  const [talk, setTalk] = useState<TalkState | null>(null);
  const talkRef = useRef<Talk | null>(null);
  const stopTalk = () => {
    talkRef.current?.stop();
    talkRef.current = null;
    engine.current?.follow(null);
    setTalk(null);
    setSaid(null);
  };
  const startTalking = async () => {
    const e = engine.current;
    if (!e) return;
    setTalk('connecting');
    setSaid(null);
    const [{ startTalk }, persona] = await Promise.all([import('./talk'), import('./persona')]);
    try {
      talkRef.current = await startTalk({
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
      });
    } catch {
      stopTalk();
      setSaid('Не получилось подключиться. Попробуй ещё раз чуть позже.');
    }
  };
  useEffect(() => () => talkRef.current?.stop(), []);
  const greeted = useRef(false);
  const speak = useRef<(l: Line) => void>(() => {});
  speak.current = (l: Line) => {
    const e = engine.current;
    // In a conversation she speaks for herself.
    if (!e || talkRef.current) return;
    setSaid(LINES[l]);
    void e.say(`${base}v2/voice/${l}.mp3`).then(() => setSaid((v) => (v === LINES[l] ? null : v)));
  };

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
          // The first tap greets her by the time of day, later ones tickle.
          onTap: () => {
            speak.current(greeted.current ? 'tickle' : hello());
            greeted.current = true;
          },
          onSpin: () => speak.current('dizzy'),
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
    if (!here) {
      stopTalk();
      setSaid(null);
    }
    engine.current.present(here, () => {
      setBusy(null);
      // Back: she says hello.
      if (here) {
        greeted.current = true;
        speak.current(hello());
      }
    });
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

  const words = talk
    ? ['Talking', TALK_WORDS[talk]]
    : busy === 'changing'
      ? ['Changing', 'Back in a moment']
      : ani.here
        ? ['With you', 'Send her off']
        : ['Away', 'Call her back'];

  return (
    <div className="ani-corner" data-ready={ready ? '' : undefined}>
      <canvas ref={canvasRef} className="ani-canvas" aria-hidden="true" />
      {said && ani.here && !chat ? (
        <div className="ani-say" role="status" data-num-ok>
          {said}
        </div>
      ) : null}
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
              Animation credits to pixiv Inc.&apos;s VRoid Project. Her voice is AI-generated
              (OpenAI).
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
          >
            <MicIcon />
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
            if (engine.current && ani.here && !talkRef.current) {
              void engine.current.say(speechUrl(t.id));
            }
          }}
        />
      ) : null}
    </div>
  );
}
