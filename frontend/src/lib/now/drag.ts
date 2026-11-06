/**
 * What can be dragged onto Ani: a row of the site carries one of these as
 * JSON under GIVE_TYPE. Nothing else is hers: links, images, files and
 * selected text are let go.
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

const KINDS: readonly string[] = ['task', 'transaction', 'note', 'event'];

/** The drag carries a site row for her (its data is unreadable before the drop, its type is not). */
export function carriesGive(dt: DataTransfer | null): boolean {
  return dt != null && Array.from(dt.types).includes(GIVE_TYPE);
}

/** What a drop carries: a site row of a kind she takes, or nothing. */
export function readGive(dt: DataTransfer): Give | null {
  const raw = dt.getData(GIVE_TYPE);
  if (!raw) return null;
  try {
    const g = JSON.parse(raw) as Give;
    if (g && typeof g === 'object' && KINDS.includes(g.kind) && typeof g.id === 'string') return g;
  } catch {
    /* not hers */
  }
  return null;
}
