/*
 * Where a tap landed on her: a part of the body, read from the bone that
 * moves the vertices under the tap most, and the material there (the face
 * and the hair are told apart from the rest of the head by material). The
 * bones are the PMX's MMD names: 左 is her left, 右 her right.
 */

export type Part =
  | 'face'
  | 'hair'
  | 'head'
  | 'neck'
  | 'shoulder'
  | 'upperArm'
  | 'forearm'
  | 'hand'
  | 'chest'
  | 'belly'
  | 'hips'
  | 'seat'
  | 'thigh'
  | 'shin'
  | 'foot'
  | 'skirt';

export type Side = 'left' | 'right' | null;

export interface Zone {
  part: Part;
  side: Side;
}

// The materials of her face, in the body and in the clothes model.
const FACE_MATERIAL =
  /^(Face|Mouth|Teeth|Tongue|Brows|Eyebrows|Eyelids|EyeLid|Lashes|Eyelashes|Eye|Emotions)/;

// First match wins; the order keeps 足首 (ankle) from reading as 足 (leg).
const BONES: [RegExp, Part][] = [
  [/^Hair/, 'hair'],
  [/^P\d-Skirt/, 'skirt'],
  [/^Breast_/, 'chest'],
  [/^Buttock_/, 'seat'],
  [/^(Jaw|[左右]目|両目)/, 'face'],
  [/^頭/, 'head'],
  [/^首/, 'neck'],
  [/(手首|指)/, 'hand'],
  [/(ひじ|手捩)/, 'forearm'],
  [/腕/, 'upperArm'],
  [/肩/, 'shoulder'],
  [/(足首|足先|つま先)/, 'foot'],
  [/ひざ/, 'shin'],
  [/足/, 'thigh'],
  [/^上半身2/, 'chest'],
  [/^上半身/, 'belly'],
  [/^下半身/, 'hips'],
];

const sideOf = (name: string): Side =>
  /^左|_L(_|\d|$)/.test(name) ? 'left' : /^右|_R(_|\d|$)/.test(name) ? 'right' : null;

/**
 * The zone of a bone (and the material under the tap): the bone itself or
 * the nearest of its parents that names a part. Bows, ribbons and petals
 * have no part of their own and take their parent's.
 */
export function zoneOf(
  bones: { name: string; parent: string | null }[],
  material: string,
): Zone | null {
  if (FACE_MATERIAL.test(material)) return { part: 'face', side: null };
  for (const b of bones) {
    for (const [re, part] of BONES)
      if (re.test(b.name)) {
        if (part === 'head' && material === 'Hair') return { part: 'hair', side: null };
        return { part, side: part === 'hair' || part === 'skirt' ? null : sideOf(b.name) };
      }
  }
  return null;
}

const WORDS: Record<Part, string> = {
  face: 'Face',
  hair: 'Hair',
  head: 'Head',
  neck: 'Neck',
  shoulder: 'Shoulder',
  upperArm: 'Upper arm',
  forearm: 'Forearm',
  hand: 'Hand',
  chest: 'Chest',
  belly: 'Belly',
  hips: 'Hips',
  seat: 'Seat',
  thigh: 'Thigh',
  shin: 'Shin',
  foot: 'Foot',
  skirt: 'Skirt',
};

/** "Left hand", "Face". */
export function zoneWords(z: Zone): string {
  const w = WORDS[z.part];
  return z.side ? `${z.side === 'left' ? 'Left' : 'Right'} ${w.toLowerCase()}` : w;
}

/** The same zone. */
export const sameZone = (a: Zone, b: Zone) => a.part === b.part && a.side === b.side;
