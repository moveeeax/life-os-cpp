import { useEffect, useState } from 'react';

import { cancelAction, confirmAction, editable, readTurn, type Action } from './chat';

/**
 * What her answer proposes, as buttons by her bubble: Telegram reply drafts
 * as choices (one sends after a tap, Edit first if wanted), other changes
 * with Confirm, and Accept all for two or more of those. Nothing changes
 * before a tap.
 */
export function AniOffers({ turn, onDone }: { turn: string; onDone: () => void }) {
  const [actions, setActions] = useState<Action[] | null>(null);
  const [editing, setEditing] = useState<{ id: string; text: string } | null>(null);
  const [busy, setBusy] = useState(false);
  useEffect(() => {
    let live = true;
    readTurn(turn)
      .then((t) => live && setActions(t.actions.filter((a) => a.status === 'pending')))
      .catch(() => live && setActions([]));
    return () => {
      live = false;
    };
  }, [turn]);
  useEffect(() => {
    if (actions && actions.length === 0) onDone();
  }, [actions, onDone]);
  if (!actions || actions.length === 0) return null;

  const drafts = actions.filter((a) => a.kind === 'tg_send');
  const changes = actions.filter((a) => a.kind !== 'tg_send');
  const run = async (f: () => Promise<unknown>) => {
    setBusy(true);
    try {
      await f();
    } catch {
      // The chat shows what became of it.
    }
    setBusy(false);
  };
  const confirm = (a: Action, text?: string) =>
    run(async () => {
      await (text === undefined ? confirmAction(a.id) : confirmAction(a.id, text));
      // A reply was picked: the other drafts to that chat are gone on the server too.
      setActions((all) =>
        (all ?? []).filter((x) => x.id !== a.id && !(a.kind === 'tg_send' && x.kind === 'tg_send')),
      );
      setEditing(null);
    });
  const cancel = (a: Action) =>
    run(async () => {
      await cancelAction(a.id);
      setActions((all) => (all ?? []).filter((x) => x.id !== a.id));
    });

  return (
    <div className="ani-offers" role="group" aria-label="What she proposes">
      {editing ? (
        <div className="ani-offer-edit">
          <textarea
            aria-label="The message"
            className="num"
            value={editing.text}
            maxLength={4000}
            rows={3}
            onChange={(e) => setEditing({ ...editing, text: e.target.value })}
          />
          <span className="ani-offer-row">
            <button
              type="button"
              className="ani-offer-main"
              disabled={busy || editing.text.trim() === ''}
              onClick={() => {
                const a = actions.find((x) => x.id === editing.id);
                if (a) void confirm(a, editing.text);
              }}
            >
              Send
            </button>
            <button type="button" onClick={() => setEditing(null)}>
              Back
            </button>
          </span>
        </div>
      ) : (
        <>
          {drafts.length > 0 && (
            <span className="ani-offer-row">
              {drafts.map((a) => (
                <button
                  key={a.id}
                  type="button"
                  className="num"
                  disabled={busy}
                  onClick={() => void confirm(a)}
                >
                  {a.text ?? a.summary}
                </button>
              ))}
              {editable(drafts[0]) && (
                <button
                  type="button"
                  onClick={() => setEditing({ id: drafts[0].id, text: drafts[0].text ?? '' })}
                >
                  Own reply…
                </button>
              )}
            </span>
          )}
          {changes.map((a) => (
            <span key={a.id} className="ani-offer-row">
              <span className="ani-offer-text num">{a.summary}</span>
              <button
                type="button"
                className="ani-offer-main"
                disabled={busy}
                onClick={() => void confirm(a)}
              >
                Yes
              </button>
              <button type="button" disabled={busy} onClick={() => void cancel(a)}>
                No
              </button>
            </span>
          ))}
          {changes.length > 1 && (
            <button
              type="button"
              className="ani-offer-main"
              disabled={busy}
              onClick={() =>
                void run(async () => {
                  for (const a of changes) await confirmAction(a.id).catch(() => undefined);
                  setActions((all) => (all ?? []).filter((x) => x.kind === 'tg_send'));
                })
              }
            >
              Accept all ({changes.length})
            </button>
          )}
        </>
      )}
    </div>
  );
}
