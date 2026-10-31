/**
 * What can be dragged onto Ani: a row of the site carries one of these as
 * JSON under GIVE_TYPE; selected text arrives as text/plain by itself.
 */
export const GIVE_TYPE = 'application/x-life-give';

export type Give =
  | { kind: 'task'; id: string; title: string }
  | { kind: 'transaction'; id: string; name: string; merchant: string | null }
  | { kind: 'note'; id: string; title: string }
  | { kind: 'event'; id: string; title: string };

/** Props that make an element draggable onto Ani with @p give. */
export function giveProps(give: Give) {
  return {
    draggable: true,
    onDragStart: (e: React.DragEvent) => {
      e.dataTransfer.setData(GIVE_TYPE, JSON.stringify(give));
      e.dataTransfer.setData('text/plain', 'title' in give ? give.title : give.name);
      e.dataTransfer.effectAllowed = 'copy';
    },
  };
}

/** What a drop carries: a site row, plain text, or nothing Ani takes. */
export function readGive(dt: DataTransfer): Give | { kind: 'text'; text: string } | null {
  const raw = dt.getData(GIVE_TYPE);
  if (raw) {
    try {
      const g = JSON.parse(raw) as Give;
      if (g && typeof g === 'object' && typeof g.kind === 'string' && typeof g.id === 'string')
        return g;
    } catch {
      return null;
    }
  }
  const text = dt.getData('text/plain').trim();
  return text ? { kind: 'text', text: text.slice(0, 500) } : null;
}
