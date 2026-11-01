import * as THREE from 'three';
import { CCDIKSolver, type IK } from 'three/examples/jsm/animation/CCDIKSolver.js';
import { MMDLoader } from 'three/examples/jsm/loaders/MMDLoader.js';

import { loadClip, type Clip } from './motion';
import { createSprings, type Collider, type SpringGroup, type Springs } from './springs';
import { createVoice } from './voice';
import { zoneOf, type Zone } from './zones';

/*
 * Ani on Now: the PMX model, posed every frame by a small director (weight
 * from one leg to the other, glances, blinks), the VRoid VRMA clips and the
 * Pic♡Pix standing poses and a few Mixamo gestures over it, spring bones for
 * the skirt, hair and ribbons, a damped jiggle for the chest and seat, and
 * the arms kept out of the body.
 *
 * She is drawn on a layer over the whole page (orthographic, at a fixed
 * height in CSS px), standing on her ledge, so she can face whatever she
 * looks at: she turns to the cursor while it moves, to the end of the text
 * while the user types in the focus field, and stands as usual looking
 * straight ahead when the cursor rests (STILL s) or is over her.
 *
 * The body is v2/Ani.pmx, in lingerie. The dress and the slip come from
 * clothes/Ani.pmx, the first model on the same skeleton (the same 628 bones in
 * the same order at the same places): its clothes are skinned to the v2
 * skeleton as they are.
 */

export type Outfit = 'p1' | 'p2' | 'lingerie' | 'bare';

export interface AniOptions {
  /** Folder of v1/ and v2/ (ends with a slash). */
  base: string;
  /** The element that takes drags and taps (the canvas lets them through). */
  hit: HTMLElement;
  /** Her height on screen, CSS px. */
  height: number;
  /** Where she stands: the middle of her ledge's top edge, viewport CSS px. */
  feet(): { x: number; y: number };
  /** The field she watches while the user types in it. */
  field?(): HTMLInputElement | HTMLTextAreaElement | null;
  /** Where her head is on screen (CSS px), every frame: for her speech bubble. */
  onHead?: (x: number, y: number) => void;
  outfit: Outfit;
  /** The bare body (signed links): mounted instead of the dressed one. */
  bare?: { model: string; texture: string };
  present: boolean;
  onReady?: () => void;
  onError?: (e: unknown) => void;
  /** A tap on her (not a drag); taps beside her body are let go. */
  onTap?: (z: Zone, at: { x: number; y: number }) => void;
  /** A long drag that spun her round more than once. */
  onSpin?: () => void;
  /** The cursor has rested on one part of her for a while (once per rest). */
  onDwell?: (z: Zone, at: { x: number; y: number }) => void;
  /** Two taps on her in quick succession (after the first one's answer). */
  onDoubleTap?: () => void;
}

/** A face she makes for a while, over her own. */
export type Feeling =
  | 'happy'
  | 'cross'
  | 'scrunch'
  | 'surprised'
  | 'sneeze'
  | 'yawn'
  | 'shy'
  | 'squint';

export interface Ani {
  /** Makes a face for `seconds`. */
  feel(f: Feeling, seconds: number): void;
  /** Leans her head into a pat on her `side` (1 her left, -1 her right). */
  tilt(side: number, seconds: number): void;
  /** Looks at a point of the page (viewport CSS px) for `seconds`. */
  glance(x: number, y: number, seconds: number): void;
  /** Turns her back on the page for `seconds`; 0 turns her round again. */
  sulk(seconds: number): void;
  /** Plays one of her clips (wave, idle_happy, spin, peace...). */
  act(name: string): void;
  /** Her mood shows in her face: -1 a little cross, 0 as usual, 1 happy. */
  mood(level: -1 | 0 | 1): void;
  /** Stops what she is saying. */
  hush(): void;
  /** Covers herself with her hands for `seconds` (bare). */
  cover(seconds: number): void;
  /** Her cheeks go pink for `seconds`, fading at the end. */
  blush(seconds: number): void;
  /** Hugs herself and shivers for `seconds` (cold). */
  hug(seconds: number): void;
  /** false sends her off through the left edge, true calls her back. */
  present(on: boolean, done?: () => void): void;
  setOutfit(o: Outfit): void;
  /** Says a recorded line, lips in time; resolves when it ends. */
  say(url: string): Promise<void>;
  /** Lips follow a live voice stream (played elsewhere); null stops. */
  follow(stream: MediaStream | null): void;
  dispose(): void;
}

// Spring settings per group (VRM units: stiffness and gravity per metre).
const SPRINGS: SpringGroup[] = [
  // The dress is a short front and a long drape behind; low stiffness and
  // real weight let the drape hang.
  {
    name: 'p1',
    test: (n) => n.startsWith('P1-Skirt'),
    settings: { stiffness: 0.6, gravity: 1.2, drag: 0.6, radius: 0.25, wind: 0.05, follow: 0.5 },
  },
  // The slip is cut close; collisions are rest-relative, so it hangs as made
  // and gives way only to motion.
  {
    name: 'p2',
    test: (n) => n.startsWith('P2-Skirt'),
    settings: { stiffness: 3, gravity: 0.2, drag: 0.7, radius: 0.1, wind: 0.03, follow: 0.8 },
  },
  {
    name: 'hair',
    test: (n) => n.startsWith('Hair'),
    settings: { stiffness: 0.9, gravity: 0.6, drag: 0.55, radius: 0.3, wind: 0.12 },
  },
  {
    name: 'ribbons',
    test: (n) => /bow|Bow|petal|rope|zip|Lace/.test(n),
    settings: { stiffness: 0.4, gravity: 1.0, drag: 0.55, radius: 0.12, wind: 0.15 },
  },
];

// The faces (ARKit shapes and MMD's 笑い in the PMX), at full weight.
const FACES: Record<Feeling, Record<string, number>> = {
  happy: {
    笑い: 0.85,
    mouthSmileLeft: 0.8,
    mouthSmileRight: 0.8,
    cheekSquintLeft: 0.4,
    cheekSquintRight: 0.4,
  },
  cross: {
    browDownLeft: 0.9,
    browDownRight: 0.9,
    mouthFrownLeft: 0.6,
    mouthFrownRight: 0.6,
    mouthSmileLeft: 0,
    mouthSmileRight: 0,
    cheekPuff: 0.5,
  },
  scrunch: {
    noseSneerLeft: 1,
    noseSneerRight: 1,
    eyeSquintLeft: 0.8,
    eyeSquintRight: 0.8,
    mouthSmileLeft: 0.5,
    mouthSmileRight: 0.5,
  },
  surprised: {
    eyeWideLeft: 0.9,
    eyeWideRight: 0.9,
    browInnerUp: 0.9,
    browOuterUpLeft: 0.6,
    browOuterUpRight: 0.6,
    jawOpen: 0.25,
    mouthSmileLeft: 0,
    mouthSmileRight: 0,
  },
  sneeze: {
    まばたき: 1,
    noseSneerLeft: 1,
    noseSneerRight: 1,
    jawOpen: 0.45,
    browInnerUp: 0.7,
    mouthSmileLeft: 0,
    mouthSmileRight: 0,
  },
  shy: {
    browInnerUp: 0.75,
    eyeLookDownLeft: 0.5,
    eyeLookDownRight: 0.5,
    mouthPressLeft: 0.5,
    mouthPressRight: 0.5,
    mouthSmileLeft: 0.2,
    mouthSmileRight: 0.2,
    mouthRollLower: 0.3,
  },
  squint: {
    eyeSquintLeft: 0.75,
    eyeSquintRight: 0.75,
    browDownLeft: 0.45,
    browDownRight: 0.45,
    mouthPressLeft: 0.35,
    mouthPressRight: 0.35,
    mouthSmileLeft: 0,
    mouthSmileRight: 0,
  },
  yawn: {
    jawOpen: 0.8,
    まばたき: 0.85,
    browInnerUp: 0.5,
    mouthFunnel: 0.3,
    mouthSmileLeft: 0,
    mouthSmileRight: 0,
  },
};
// The shapes of those faces that her own face does not set every frame.
const FACE_ONLY = [...new Set(Object.values(FACES).flatMap((f) => Object.keys(f)))].filter(
  (n) => !['まばたき', 'mouthSmileLeft', 'mouthSmileRight'].includes(n),
);
// Her back to the page, sulking (rad past her usual turn).
const SULK = 2.7;

// The PMX is about 21.8 units tall.
const MODEL_HEIGHT = 21.8;
// Her turn when she looks ahead.
const FACE = 0.25;
// A cursor still this long (s) is let go of.
const STILL = 5;
// Going: a wave goodbye (the greeting clip from WAVE_FROM), then she fades
// out over the last FADE_OUT s of LEAVE. Coming back: she fades in over
// FADE_IN s rising RISE from below, then greets.
const LEAVE = 2.3;
const FADE_OUT = 0.8;
const ARRIVE = 1.0;
const FADE_IN = 0.8;
const RISE = 0.5;
const WAVE_FROM = 2.3;

// Motion clips from pixiv's VRoid VRMA pack; from: seconds to skip (the
// greeting starts sitting on the floor).
const CLIPS: Record<string, { file: string; from?: number }> = {
  show: { file: 'VRMA_01' },
  greet: { file: 'VRMA_02', from: 2.3 },
  peace: { file: 'VRMA_03' },
  shoot: { file: 'VRMA_04' },
  spin: { file: 'VRMA_05' },
  pose: { file: 'VRMA_06' },
  squat: { file: 'VRMA_07' },
};
// Gestures from Mixamo, baked to VRMA under mixamo/.
for (const n of ['idle_happy', 'idle_cat', 'wave']) CLIPS[n] = { file: `mixamo/${n}` };
// No squat: on her ledge it read as hanging in the air.
const EMOTES = ['show', 'peace', 'spin', 'pose', 'shoot', 'idle_happy', 'idle_cat', 'wave'];
// Standing poses from the Pic♡Pix Free Pose set (Unity humanoid poses baked
// to VRMA, each held for 6 s), under poses/pose<n>. Poses 20 to 29 sit or
// kneel on a floor (hips 0.15 to 0.47 m against 0.95 standing), 16 and 17
// are caught mid-jump, 18 kneels and 30 sits on a chair that is not there:
// on her glass ledge they read as hanging in the air, so they are not used.
const POSES: string[] = [];
const OFF_LEDGE = new Set([16, 17, 18, 30]);
for (let n = 1; n <= 30; n++) {
  if ((n >= 20 && n <= 29) || OFF_LEDGE.has(n)) continue;
  POSES.push(`pose${n}`);
  CLIPS[`pose${n}`] = { file: `poses/pose${n}` };
}

// Files under the base.
const BODY_PMX = 'v2/Ani.pmx';
// The MMD morph names the engine uses, and their English names.
const MORPH_EN: Record<string, string> = {
  まばたき: 'Blink',
  笑い: 'Smile Eyes',
  あ: 'A',
  い: 'I',
  う: 'U',
  え: 'E',
  お: 'O',
};
// Bare: her head on another body (outfits are made for v2's, so it has none).
// The dressed v1 model and its textures. v1/ in the bucket holds another
// variant of it (every garment at alpha 0, other body and outfit textures),
// cached as immutable, so the dressed one has its own folder.
const CLOTHES_PMX = 'clothes/Ani.pmx';
const MOTIONS = 'v2/motions/';

// The v1 outfits. v1 switches them with the material morph P2-Change, which
// MMDLoader does not apply: P2-Change multiplies the P1 materials' alpha by
// 0 and adds 1 to the P2 ones. Its own skin, face and hair are not used.
const P1 = [
  'P1-BackBow',
  'P1-TopCloth',
  'P1-Vest',
  'P1-Vest-Metal',
  'P1-UnderPants',
  'P1-InnerSkirt',
  'P1-OutSkirt',
  'Shared-Glove',
];
const P2 = ['P2-Glove(Hide)', 'P2-Top(Hide)', 'P2-Panties(Hide)', 'P2-Skirt'];
// Worn with either v1 outfit.
const SHARED = ['Shared-Socks-Lace', 'Shared-Collar', 'Shared-Boots', 'Hair-Band'];
// The v2 body's own lingerie.
const LINGERIE = ['Lingerie'];
const LEG = ['左足', '右足', '左ひざ', '右ひざ', '左足首', '右足首'];

const smooth = (a: number) => a * a * (3 - 2 * a);
const clamp01 = (a: number) => Math.min(1, Math.max(0, a));
const rnd = (a: number, b: number) => a + Math.random() * (b - a);

interface MmdGrant {
  index: number;
  parentIndex: number;
  ratio: number;
}

type Walk = 'here' | 'leaving' | 'away' | 'arriving';

export function mountAni(canvas: HTMLCanvasElement, opts: AniOptions): Ani {
  // Reduced motion: a still pose, no idle moves, no hair or cloth in the
  // wind; followed live, so a change in the system settings applies at once.
  const motion = matchMedia('(prefers-reduced-motion: reduce)');
  let still = motion.matches;
  const renderer = new THREE.WebGLRenderer({ canvas, alpha: true, antialias: true });
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
  // The toon materials read right with linear output.
  renderer.outputColorSpace = THREE.LinearSRGBColorSpace;
  const scene = new THREE.Scene();
  // Model units per CSS px; y up from the viewport's bottom edge.
  const S = MODEL_HEIGHT / opts.height;
  const cam = new THREE.OrthographicCamera(0, 1, 1, 0, 1, 400);
  scene.add(new THREE.AmbientLight(0xffffff, 0.9));
  const key = new THREE.DirectionalLight(0xffe2d6, 1.1);
  key.position.set(-6, 12, 10);
  scene.add(key);
  const rim = new THREE.DirectionalLight(0x9fb8ff, 0.9);
  rim.position.set(8, 10, -6);
  scene.add(rim);

  let VH = 1;
  const place = () => {
    const w = canvas.clientWidth;
    const h = canvas.clientHeight;
    if (!w || !h) return;
    VH = h;
    renderer.setSize(w, h, false);
    cam.left = 0;
    cam.right = w * S;
    cam.top = h * S;
    cam.bottom = 0;
    cam.position.set(0, 0, 150);
    cam.lookAt(0, 0, 0);
    cam.updateProjectionMatrix();
  };
  // Her feet, moved to her ledge every frame.
  const root = new THREE.Group();
  scene.add(root);
  const resize = new ResizeObserver(() => {
    place();
    wake();
  });
  resize.observe(canvas);

  const clock = new THREE.Clock();
  const voice = createVoice();
  let disposed = false;
  let raf = 0;
  let mesh: THREE.SkinnedMesh | null = null;
  let ik: CCDIKSolver | null = null;
  let springs: Springs | null = null;
  let settle = false;
  // A live voice (the Realtime call) moves her lips for as long as it runs.
  let following = false;
  // Until when (clock s) the loop draws every frame whatever else holds.
  let wakeUntil = 0;
  const wake = (seconds = 1.5) => {
    wakeUntil = Math.max(wakeUntil, clock.getElapsedTime() + seconds);
  };
  const B: Record<string, THREE.Bone> = {};
  const baseQ: Record<string, THREE.Quaternion> = {};
  const baseP: Record<string, THREE.Vector3> = {};
  const BIND: Record<string, THREE.Vector3> = {};
  const AX: Record<
    string,
    { arm: THREE.Vector3; flex: THREE.Vector3; upper: number; lower: number }
  > = {};
  let grants: { bone: THREE.Bone; from: THREE.Bone; ratio: number }[] = [];
  let morphs: Record<string, number> = {};
  let outfit: Outfit = opts.outfit;
  let clothes: THREE.SkinnedMesh | null = null;

  const walk = {
    state: (opts.present ? 'here' : 'away') as Walk,
    t0: 0,
    done: undefined as (() => void) | undefined,
  };

  // She turns to the cursor while it moves (viewport CSS px).
  const pointer = { x: -1, y: -1, at: -10 };
  // A cursor flying at her face startles her (px/s, px, s between).
  const FLINCH_SPEED = 2200;
  const FLINCH_NEAR = 160;
  const FLINCH_EVERY = 4;
  const head = { x: -1e4, y: -1e4 };
  const onPointer = (e: PointerEvent) => {
    const now = clock.getElapsedTime();
    const dt = now - pointer.at;
    if (dt > 0 && dt < 0.1 && pointer.x >= 0 && walk.state === 'here') {
      const vx = (e.clientX - pointer.x) / dt;
      const vy = (e.clientY - pointer.y) / dt;
      const dx = head.x - e.clientX;
      const dy = head.y - e.clientY;
      if (
        Math.hypot(vx, vy) > FLINCH_SPEED &&
        Math.hypot(dx, dy) < FLINCH_NEAR &&
        vx * dx + vy * dy > 0 &&
        now - touch.flinchAt > FLINCH_EVERY
      ) {
        touch.flinchAt = now;
        if (now >= touch.feelUntil) {
          touch.feel = 'surprised';
          touch.feelUntil = now + 0.9;
        }
      }
    }
    pointer.x = e.clientX;
    pointer.y = e.clientY;
    pointer.at = now;
  };
  addEventListener('pointermove', onPointer);
  // The cursor resting on her: the zone under it, read a few times a second
  // while it is over her, reported once it has stayed on a part DWELL s.
  const DWELL = 1.5;
  const dwell = { part: '', since: 0, told: false, checked: -10 };
  function watchDwell(now: number) {
    if (now - dwell.checked < 0.2) return;
    dwell.checked = now;
    const r = hit.getBoundingClientRect();
    const inside =
      walk.state === 'here' &&
      !spin.drag &&
      pointer.x >= r.left &&
      pointer.x <= r.right &&
      pointer.y >= r.top &&
      pointer.y <= r.bottom;
    const z = inside ? touched(pointer.x, pointer.y) : null;
    const part = z ? `${z.part}` : '';
    if (part !== dwell.part) {
      dwell.part = part;
      dwell.since = now;
      dwell.told = false;
      return;
    }
    if (z && !dwell.told && now - dwell.since > DWELL) {
      dwell.told = true;
      opts.onDwell?.(z, { x: pointer.x, y: pointer.y });
    }
  }
  // And to the text while the user types.
  let typedAt = -10;
  const onInput = () => (typedAt = clock.getElapsedTime());
  addEventListener('input', onInput, true);
  const meter = document.createElement('canvas').getContext('2d');
  /** The end of what is typed in the field, viewport CSS px. */
  function caret(f: HTMLInputElement | HTMLTextAreaElement) {
    const r = f.getBoundingClientRect();
    const cs = getComputedStyle(f);
    let w = 0;
    if (meter) {
      meter.font = `${cs.fontSize} ${cs.fontFamily}`;
      const line =
        f.value
          .slice(0, f.selectionStart ?? f.value.length)
          .split('\n')
          .pop() ?? '';
      w = meter.measureText(line).width;
    }
    // A centred field grows from its middle.
    const left =
      cs.textAlign === 'center'
        ? r.left + r.width / 2 - w / 2
        : r.left + parseFloat(cs.paddingLeft);
    return { x: Math.min(r.right - 8, left + w), y: r.top + r.height / 2 };
  }

  // A drag turns her around (with a little inertia); after a few idle
  // seconds she turns back. A tap plays the greeting.
  const spin = { yaw: 0, vel: 0, idleAt: 0, drag: null as { x: number; moved: number } | null };
  const hit = opts.hit;
  const onDown = (e: PointerEvent) => {
    if (!mesh || walk.state !== 'here') return;
    hit.setPointerCapture(e.pointerId);
    spin.drag = { x: e.clientX, moved: 0 };
    spin.vel = 0;
    hit.dataset.dragging = '';
  };
  const onMove = (e: PointerEvent) => {
    if (!spin.drag) return;
    const dx = e.clientX - spin.drag.x;
    spin.drag.x = e.clientX;
    spin.drag.moved += Math.abs(dx);
    spin.yaw += dx * 0.012;
    spin.vel = dx * 0.012;
  };
  // Two taps close together, with no third after them, are a double tap;
  // a flurry of taps is not (it makes her dizzy).
  const DOUBLE_TAP = 0.35;
  const burst = { n: 0, at: -10, timer: 0 };
  const onUp = (e: PointerEvent) => {
    if (!spin.drag) return;
    const z = spin.drag.moved < 5 ? touched(e.clientX, e.clientY) : null;
    if (z) {
      spin.vel = 0;
      opts.onTap?.(z, { x: e.clientX, y: e.clientY });
      const now = clock.getElapsedTime();
      burst.n = now - burst.at < DOUBLE_TAP ? burst.n + 1 : 1;
      burst.at = now;
      clearTimeout(burst.timer);
      if (burst.n === 2)
        burst.timer = window.setTimeout(() => {
          if (burst.n === 2) opts.onDoubleTap?.();
        }, DOUBLE_TAP * 1000);
    } else if (spin.drag.moved * 0.012 > Math.PI * 2) opts.onSpin?.();
    spin.drag = null;
    spin.idleAt = clock.getElapsedTime();
    delete hit.dataset.dragging;
  };
  hit.addEventListener('pointerdown', onDown);
  hit.addEventListener('pointermove', onMove);
  hit.addEventListener('pointerup', onUp);
  hit.addEventListener('pointercancel', onUp);

  // A tap is read by a ray from the camera into her meshes as posed now:
  // the bone that moves the triangle under it most names the zone.
  const ray = new THREE.Raycaster();
  const zones = new Map<string, Zone | null>();
  const shown = (m: THREE.SkinnedMesh, i: number | undefined) => {
    const mat = (m.material as THREE.Material[])[i ?? 0];
    return mat && mat.visible && mat.opacity > 0 ? mat : null;
  };
  function zoneOfFace(m: THREE.SkinnedMesh, a: number, b: number, c: number, material: string) {
    const si = m.geometry.attributes.skinIndex as THREE.BufferAttribute;
    const sw = m.geometry.attributes.skinWeight as THREE.BufferAttribute;
    const w = new Map<number, number>();
    for (const v of [a, b, c])
      for (let k = 0; k < 4; k++) {
        const bone = si.getComponent(v, k);
        w.set(bone, (w.get(bone) ?? 0) + sw.getComponent(v, k));
      }
    let top = 0;
    let most = -1;
    for (const [bone, x] of w)
      if (x > most) {
        most = x;
        top = bone;
      }
    const key = `${top}|${material}`;
    if (!zones.has(key)) {
      const chain: { name: string; parent: string | null }[] = [];
      for (
        let o: THREE.Object3D | null = m.skeleton.bones[top];
        o && (o as THREE.Bone).isBone;
        o = o.parent
      )
        chain.push({ name: o.name, parent: o.parent?.name ?? null });
      zones.set(key, zoneOf(chain, material));
    }
    return zones.get(key) ?? null;
  }
  function touched(x: number, y: number): Zone | null {
    if (!mesh) return null;
    const r = canvas.getBoundingClientRect();
    ray.setFromCamera(
      new THREE.Vector2(((x - r.left) / r.width) * 2 - 1, -((y - r.top) / r.height) * 2 + 1),
      cam,
    );
    const targets = clothes ? [mesh, clothes] : [mesh];
    // The spheres follow the pose.
    for (const t of targets) t.computeBoundingSphere();
    for (const h of ray.intersectObjects(targets, false)) {
      const m = h.object as THREE.SkinnedMesh;
      const mat = shown(m, h.face?.materialIndex);
      if (!mat || !h.face) continue;
      const z = zoneOfFace(m, h.face.a, h.face.b, h.face.c, mat.name);
      if (z) return z;
    }
    return null;
  }
  // The files are PNG where the PMX names BMP textures. The bare model and
  // its body texture come by signed links; its head textures are v2's.
  const manager = new THREE.LoadingManager();
  manager.setURLModifier((u) =>
    opts.bare && /kanata_body\.png$/i.test(u) ? opts.bare.texture : u.replace(/\.bmp$/i, '.png'),
  );
  const loader = new MMDLoader(manager);
  const fetchModel = (l: MMDLoader, url: string) =>
    new Promise<THREE.SkinnedMesh>((ok, fail) => l.load(url, ok, undefined, fail));
  const load = (file: string) => fetchModel(loader, `${opts.base}${file}`);
  const body = opts.bare
    ? fetchModel(new MMDLoader(manager).setResourcePath(`${opts.base}v2/`), opts.bare.model)
    : load(BODY_PMX);
  Promise.all([body, load(CLOTHES_PMX)]).then(
    ([m, v1]) => {
      if (disposed) return;
      setUp(m, v1);
      wake(2);
      opts.onReady?.();
    },
    (e) => opts.onError?.(e),
  );

  // Her blush: a pink mask on the face texture's cheeks, faded in by a
  // uniform (0..1).
  const blushU = { value: 0 };
  function addBlush(m: THREE.SkinnedMesh) {
    const mats = m.material as THREE.Material[];
    const fi = mats.findIndex((x) => x.name === 'Face_Skin');
    const face = mats[fi] as THREE.Material & { map?: THREE.Texture | null };
    if (fi < 0 || !face.map) return;
    const g = m.geometry;
    const grp = g.groups.find((x) => x.materialIndex === fi);
    const pos = g.attributes.position as THREE.BufferAttribute;
    const uv = g.attributes.uv as THREE.BufferAttribute;
    if (!grp || !g.index || !uv) return;
    const verts = new Set<number>();
    for (let k = grp.start; k < grp.start + grp.count; k++) verts.add(g.index.getX(k));
    // Front is where the eyes are, off the head bone.
    const local = (n: string) => m.worldToLocal(B[n].getWorldPosition(new THREE.Vector3()));
    const eye = local('左目');
    const fz = Math.sign(eye.z - local('頭').z) || 1;
    const cheek = (sx: number) => {
      // The front-most face vertex under the eye, a little out from it.
      let best = -1;
      let bz = -1e9;
      for (const v of verts) {
        const x = pos.getX(v);
        const y = pos.getY(v);
        if (
          Math.abs(x - sx * (Math.abs(eye.x) + 0.12)) < 0.12 &&
          Math.abs(y - (eye.y - 0.42)) < 0.12
        ) {
          const z = pos.getZ(v) * fz;
          if (z > bz) {
            bz = z;
            best = v;
          }
        }
      }
      return best;
    };
    const size = 512;
    const cv = document.createElement('canvas');
    cv.width = cv.height = size;
    const cx = cv.getContext('2d');
    if (!cx) return;
    for (const sx of [1, -1]) {
      const v = cheek(sx);
      if (v < 0) continue;
      // UV per model unit near the cheek, for the spot's size.
      let du = 0;
      for (const w of verts) {
        const d = Math.hypot(pos.getX(w) - pos.getX(v), pos.getY(w) - pos.getY(v));
        if (d > 0.15 && d < 0.25) {
          du = Math.hypot(uv.getX(w) - uv.getX(v), uv.getY(w) - uv.getY(v)) / d;
          break;
        }
      }
      const r = Math.max(0.01, du * 0.19) * size;
      const px = uv.getX(v) * size;
      const py = (face.map.flipY ? 1 - uv.getY(v) : uv.getY(v)) * size;
      const gr = cx.createRadialGradient(px, py, 0, px, py, r);
      gr.addColorStop(0, 'rgba(255,255,255,1)');
      gr.addColorStop(1, 'rgba(255,255,255,0)');
      cx.fillStyle = gr;
      cx.save();
      cx.translate(px, py);
      cx.scale(1.35, 0.8);
      cx.translate(-px, -py);
      cx.beginPath();
      cx.arc(px, py, r, 0, Math.PI * 2);
      cx.fill();
      cx.restore();
    }
    const mask = new THREE.CanvasTexture(cv);
    mask.flipY = face.map.flipY;
    mask.channel = face.map.channel;
    face.onBeforeCompile = (sh) => {
      sh.uniforms.blushMap = { value: mask };
      sh.uniforms.blush = blushU;
      sh.fragmentShader = sh.fragmentShader
        .replace(
          'void main() {',
          'uniform sampler2D blushMap;\nuniform float blush;\nvoid main() {',
        )
        .replace(
          '#include <map_fragment>',
          '#include <map_fragment>\n  diffuseColor.rgb = mix(diffuseColor.rgb, diffuseColor.rgb * vec3(1.0, 0.62, 0.66), texture2D(blushMap, vMapUv).a * blush);',
        );
    };
    face.needsUpdate = true;
  }

  function setUp(m: THREE.SkinnedMesh, v1: THREE.SkinnedMesh) {
    mesh = m;
    m.rotation.y = FACE;
    m.visible = walk.state === 'here';
    root.add(m);
    for (const b of m.skeleton.bones) {
      B[b.name] = b;
      baseQ[b.name] = b.quaternion.clone();
      baseP[b.name] = b.position.clone();
    }
    morphs = { ...(m.morphTargetDictionary ?? {}) };
    // A model with English morph names (the bare one): the MMD names the
    // engine uses find them too.
    for (const [jp, en] of Object.entries(MORPH_EN))
      if (morphs[jp] == null && morphs[en] != null) morphs[jp] = morphs[en];
    m.updateMatrixWorld(true);
    addBlush(m);
    // v1's clothes on the v2 skeleton: v1's skin indices name the same bones.
    const worn = new Set([...P1, ...P2, ...SHARED]);
    for (const mat of v1.material as THREE.Material[]) if (!worn.has(mat.name)) mat.visible = false;
    clothes = new THREE.SkinnedMesh(v1.geometry, v1.material);
    clothes.frustumCulled = false;
    m.add(clothes);
    clothes.bind(m.skeleton, new THREE.Matrix4());
    hugLace(clothes, m);
    // The skirts, frills and bows are single-sided in the PMX: once the
    // springs swing them, their inner side faces the camera and vanishes.
    for (const mat of clothes.material as THREE.Material[])
      if (/Skirt|BackBow|UnderPants|Socks-Lace/.test(mat.name)) mat.side = THREE.DoubleSide;
    m.updateMatrixWorld(true);
    for (const b of m.skeleton.bones)
      BIND[b.name] = m.worldToLocal(b.getWorldPosition(new THREE.Vector3()));
    // Arm axes from the bind pose, in the model's space (bind rotations are
    // identity in MMD): the elbow flexes about forearm x forward.
    for (const side of ['左', '右']) {
      AX[side] = {
        arm: at(side + 'ひじ')
          .sub(at(side + '腕'))
          .normalize(),
        flex: at(side + '手首')
          .sub(at(side + 'ひじ'))
          .normalize()
          .cross(new THREE.Vector3(0, 0, 1))
          .normalize(),
        upper: at(side + 'ひじ').distanceTo(at(side + '腕')),
        lower: at(side + '手首').distanceTo(at(side + 'ひじ')),
      };
    }
    // Partial twist bones (腕捩1-3, 手捩1-3) copy a share of their parent
    // twist by grant; MMDLoader leaves grants to the animation helper.
    const mmd = m.geometry.userData.MMD as { grants: MmdGrant[]; iks: IK[] };
    grants = mmd.grants
      .filter((g) => /捩\d$/.test(m.skeleton.bones[g.index].name))
      .map((g) => ({
        bone: m.skeleton.bones[g.index],
        from: m.skeleton.bones[g.parentIndex],
        ratio: g.ratio,
      }));
    // The model's own leg IK: the feet stay put while the hips sway.
    ik = new CCDIKSolver(m, mmd.iks);
    // The moves load now; a pose loads when she first picks it.
    for (const name of Object.keys(CLIPS)) if (!POSES.includes(name)) void clipOf(name);
    // v2 has rigid bodies for its hair and chest only; the clothes' bones
    // have theirs in v1. Made under reduced motion too (they rest there,
    // see frame), so turning it off later brings the sway back.
    springs = createSprings(m, SPRINGS, [v1]);
    // Calibrate in the bind pose, against every collider.
    m.updateMatrixWorld(true);
    placeColliders();
    springs.calibrate(colliders);
    const V = (x: number, y: number, z: number) => new THREE.Vector3(x, y, z);
    springs.setLeads([
      {
        thigh: B['左足D'],
        hips: B['下半身'],
        a: V(1.16, 12.65, -0.11),
        b: V(0.91, 7.26, -0.13),
        r: 1.15,
      },
      {
        thigh: B['右足D'],
        hips: B['下半身'],
        a: V(-1.16, 12.65, -0.11),
        b: V(-0.91, 7.26, -0.13),
        r: 1.15,
      },
    ]);
    dress(outfit);
  }

  function dress(o: Outfit) {
    outfit = o;
    if (!mesh || !clothes) return;
    for (const mat of clothes.material as THREE.Material[]) {
      // Worn means opaque, whatever alpha the PMX gives the material.
      if (P1.includes(mat.name)) {
        mat.visible = o === 'p1';
        mat.opacity = 1;
      }
      if (SHARED.includes(mat.name)) {
        mat.visible = o !== 'lingerie' && o !== 'bare';
        mat.opacity = 1;
      }
      if (P2.includes(mat.name)) {
        mat.visible = o === 'p2';
        mat.opacity = o === 'p2' ? 1 : 0;
      }
    }
    for (const mat of mesh.material as THREE.Material[])
      if (LINGERIE.includes(mat.name)) {
        mat.visible = o === 'lingerie';
        // It lies on the skin: seen from the side the skin would win near ties.
        mat.polygonOffset = true;
        mat.polygonOffsetFactor = -1;
        mat.polygonOffsetUnits = -4;
      }
    // Only the worn skirt swings.
    if (springs) {
      springs.off = new Set(['p1', 'p2'].filter((g) => g !== o));
      settle = true;
    }
  }

  // Lace lies on the skin. The petticoat (P1-InnerSkirt) shared the outer
  // skirt's bones and flared with it; the frills of the lace shorts
  // (P1-UnderPants) stood off the legs on the hip bone alone, so the seat
  // ate them when a leg moved; the garter band swung on ribbon chains. The
  // v2 hips and thighs are up to 0.17 off v1's, so lace made for v1 sank
  // into the v2 skin in places and stood off it in others. Each of their
  // vertices (and the slip's panties) goes to the nearest point of the v2
  // skin: it takes the bone weights the skin has there (its triangle's three
  // vertices blended by barycentric weight, the four strongest bones kept),
  // so it moves as that point of the skin does, and sits on it along the
  // skin's normal: 0.03 out, plus a twentieth of any depth it stood off.
  // The lace is drawn with a depth bias, so the skin wins no near tie.
  function hugLace(cloth: THREE.SkinnedMesh, body: THREE.SkinnedMesh) {
    const g = cloth.geometry;
    const bg = body.geometry;
    if (!g.index || !bg.index) return;
    const groupsOf = (geo: THREE.BufferGeometry, mesh: THREE.SkinnedMesh, names: string[]) => {
      const mats = mesh.material as THREE.Material[];
      return geo.groups.filter((grp) => names.includes(mats[grp.materialIndex ?? 0].name));
    };
    const pos = g.attributes.position as THREE.BufferAttribute;
    const nrm = g.attributes.normal as THREE.BufferAttribute;
    const si = g.attributes.skinIndex as THREE.BufferAttribute;
    const sw = g.attributes.skinWeight as THREE.BufferAttribute;
    const bPos = bg.attributes.position as THREE.BufferAttribute;
    const bNrm = bg.attributes.normal as THREE.BufferAttribute;
    const bSi = bg.attributes.skinIndex as THREE.BufferAttribute;
    const bSw = bg.attributes.skinWeight as THREE.BufferAttribute;
    // The skin's triangles around each of its vertices, and a grid of the
    // vertices for the nearest-vertex search.
    const bIdx = bg.index;
    const around = new Map<number, number[]>();
    const CELL = 0.6;
    const grid = new Map<string, number[]>();
    const cellOf = (x: number, y: number, z: number) =>
      `${Math.floor(x / CELL)},${Math.floor(y / CELL)},${Math.floor(z / CELL)}`;
    for (const grp of groupsOf(bg, body, ['Body_Skin']))
      for (let k = grp.start; k < grp.start + grp.count; k += 3)
        for (let e = 0; e < 3; e++) {
          const v = bIdx.getX(k + e);
          let tris = around.get(v);
          if (!tris) {
            around.set(v, (tris = []));
            const key = cellOf(bPos.getX(v), bPos.getY(v), bPos.getZ(v));
            const cell = grid.get(key);
            if (cell) cell.push(v);
            else grid.set(key, [v]);
          }
          tris.push(k);
        }
    const p = new THREE.Vector3();
    const sp = new THREE.Vector3();
    const cp = new THREE.Vector3();
    const sn = new THREE.Vector3();
    const bary = new THREE.Vector3();
    const tri = new THREE.Triangle();
    const corner = [0, 0, 0];
    const lace = ['P1-InnerSkirt', 'P1-UnderPants', 'Shared-Socks-Lace', 'P2-Panties(Hide)'];
    const done = new Set<number>();
    for (const grp of groupsOf(g, cloth, lace))
      for (let k = grp.start; k < grp.start + grp.count; k++) {
        const v = g.index.getX(k);
        if (done.has(v)) continue;
        done.add(v);
        p.fromBufferAttribute(pos, v);
        let best = -1;
        let bd = Infinity;
        const cx = Math.floor(p.x / CELL);
        const cy = Math.floor(p.y / CELL);
        const cz = Math.floor(p.z / CELL);
        for (let r = 1; best < 0 && r <= 4; r++)
          for (let x = cx - r; x <= cx + r; x++)
            for (let y = cy - r; y <= cy + r; y++)
              for (let z = cz - r; z <= cz + r; z++)
                for (const u of grid.get(`${x},${y},${z}`) ?? []) {
                  const d = sp.fromBufferAttribute(bPos, u).distanceToSquared(p);
                  if (d < bd) {
                    bd = d;
                    best = u;
                  }
                }
        if (best < 0) continue;
        // The nearest point on the triangles around that vertex.
        let bt = -1;
        bd = Infinity;
        for (const t of around.get(best)!) {
          tri.a.fromBufferAttribute(bPos, bIdx.getX(t));
          tri.b.fromBufferAttribute(bPos, bIdx.getX(t + 1));
          tri.c.fromBufferAttribute(bPos, bIdx.getX(t + 2));
          tri.closestPointToPoint(p, cp);
          const d = cp.distanceToSquared(p);
          if (d < bd) {
            bd = d;
            bt = t;
            sp.copy(cp);
          }
        }
        for (let e = 0; e < 3; e++) corner[e] = bIdx.getX(bt + e);
        tri.a.fromBufferAttribute(bPos, corner[0]);
        tri.b.fromBufferAttribute(bPos, corner[1]);
        tri.c.fromBufferAttribute(bPos, corner[2]);
        tri.getBarycoord(sp, bary);
        sn.set(0, 0, 0);
        const weights = new Map<number, number>();
        for (let e = 0; e < 3; e++) {
          const b = bary.getComponent(e);
          sn.addScaledVector(cp.fromBufferAttribute(bNrm, corner[e]), b);
          for (let c = 0; c < 4; c++) {
            const w = bSw.getComponent(corner[e], c) * b;
            if (w <= 0) continue;
            const bone = bSi.getComponent(corner[e], c);
            weights.set(bone, (weights.get(bone) ?? 0) + w);
          }
        }
        sn.normalize();
        const depth = cp.subVectors(p, sp).dot(sn);
        const want = 0.03 + Math.max(0, depth - 0.03) * 0.05;
        p.copy(sp).addScaledVector(sn, want);
        pos.setXYZ(v, p.x, p.y, p.z);
        if (depth > 0.03) nrm.setXYZ(v, sn.x, sn.y, sn.z);
        const top = [...weights].sort((a, b) => b[1] - a[1]).slice(0, 4);
        const sum = top.reduce((a, [, w]) => a + w, 0) || 1;
        for (let c = 0; c < 4; c++) {
          si.setComponent(v, c, top[c]?.[0] ?? 0);
          sw.setComponent(v, c, (top[c]?.[1] ?? 0) / sum);
        }
      }
    pos.needsUpdate = nrm.needsUpdate = si.needsUpdate = sw.needsUpdate = true;
    for (const mat of cloth.material as THREE.Material[])
      if (lace.includes(mat.name)) {
        mat.polygonOffset = true;
        mat.polygonOffsetFactor = -1;
        mat.polygonOffsetUnits = -4;
      }
  }

  const q = new THREE.Quaternion();
  const eu = new THREE.Euler();
  const _off = new THREE.Vector3();
  const _id = new THREE.Quaternion();
  const pose = (n: string, x: number, y: number, z: number) => {
    eu.set(x, y, z);
    B[n].quaternion.copy(baseQ[n]).multiply(q.setFromEuler(eu));
  };
  const turn = (n: string, axis: THREE.Vector3, angle: number) =>
    B[n].quaternion.copy(baseQ[n]).multiply(q.setFromAxisAngle(axis, angle));
  const move = (n: string, x: number, y: number, z: number) =>
    B[n].position.copy(baseP[n]).add(_off.set(x, y, z));
  /** Mesh-space position of a bone, from the current matrices. */
  const at = (n: string) => mesh!.worldToLocal(B[n].getWorldPosition(new THREE.Vector3()));
  const morph = (n: string, v: number) => {
    const i = morphs[n];
    if (i != null && mesh?.morphTargetInfluences) mesh.morphTargetInfluences[i] = v;
  };

  // Two-bone IK for an arm: the wrist at T, the elbow towards the pole P.
  const _Z = new THREE.Vector3(0, 0, 1);
  const _u = new THREE.Vector3();
  const _f = new THREE.Vector3();
  const _b = new THREE.Vector3();
  const _e = new THREE.Vector3();
  const _r1 = new THREE.Quaternion();
  const _rt = new THREE.Quaternion();
  const _pq = new THREE.Quaternion();
  const _mq = new THREE.Quaternion();
  const _qa = new THREE.Quaternion();
  const _qe = new THREE.Quaternion();
  function solveArm(side: string, S: THREE.Vector3, T: THREE.Vector3, P: THREE.Vector3) {
    const ax = AX[side];
    const L1 = ax.upper;
    const L2 = ax.lower;
    const d = _e.subVectors(T, S);
    const dist = THREE.MathUtils.clamp(d.length(), Math.abs(L1 - L2) + 0.01, L1 + L2 - 0.01);
    const dn = d.normalize();
    // The elbow by the law of cosines, bent towards the pole.
    const a = (L1 * L1 + dist * dist - L2 * L2) / (2 * dist);
    const hgt = Math.sqrt(Math.max(0, L1 * L1 - a * a));
    const pp = _b.subVectors(P, S);
    pp.addScaledVector(dn, -pp.dot(dn)).normalize();
    const E = S.clone().addScaledVector(dn, a).addScaledVector(pp, hgt);
    const Tc = S.clone().addScaledVector(dn, dist);
    _u.subVectors(E, S).normalize();
    _f.subVectors(Tc, E).normalize();
    // Upper arm: the bind axis onto u, then a turn about u so the bind
    // bending direction (+z) lies in the plane of the bend.
    _r1.setFromUnitVectors(ax.arm, _u);
    const b1 = _Z.clone().applyQuaternion(_r1);
    b1.addScaledVector(_u, -b1.dot(_u)).normalize();
    const bend = _b.copy(_f).addScaledVector(_u, -_f.dot(_u)).normalize();
    _rt.setFromUnitVectors(b1, bend);
    const world = _rt.multiply(_r1);
    B[side + '腕'].parent!.getWorldQuaternion(_pq);
    _pq.premultiply(mesh!.getWorldQuaternion(_mq).invert());
    _qa.copy(_pq).invert().multiply(world);
    _qe.setFromAxisAngle(ax.flex, Math.acos(THREE.MathUtils.clamp(_u.dot(_f), -1, 1)));
    B[side + '腕'].quaternion.copy(_qa);
    B[side + 'ひじ'].quaternion.copy(_qe);
  }

  // The body as capsules for the arms: thigh skin sits 0.93-1.17 from the
  // thigh bone, the chest 0.98-1.45 from the spine, the hips up to 1.9 from
  // 下半身 (measured on the PMX vertices). Ends are points on a bone, in
  // bind-pose model space.
  const BODY: {
    a: [string, number[]];
    b: [string, number[]];
    r: number;
    A: THREE.Vector3;
    Bp: THREE.Vector3;
  }[] = [
    { a: ['左足D', [1.16, 12.65, -0.11]], b: ['左ひざD', [0.91, 7.26, -0.13]], r: 1.2 },
    { a: ['右足D', [-1.16, 12.65, -0.11]], b: ['右ひざD', [-0.91, 7.26, -0.13]], r: 1.2 },
    { a: ['左ひざD', [0.91, 7.26, -0.13]], b: ['左足首D', [0.79, 1.71, -0.68]], r: 0.8 },
    { a: ['右ひざD', [-0.91, 7.26, -0.13]], b: ['右足首D', [-0.79, 1.71, -0.68]], r: 0.8 },
    { a: ['下半身', [-1.0, 12.9, -0.15]], b: ['下半身', [1.0, 12.9, -0.15]], r: 1.5 },
    { a: ['上半身', [0, 14.3, 0]], b: ['上半身2', [0, 16.6, 0.1]], r: 1.35 },
    { a: ['上半身2', [0, 16.6, 0.1]], b: ['首', [0, 18.2, -0.5]], r: 1.4 },
  ].map((c) => ({
    ...(c as { a: [string, number[]]; b: [string, number[]]; r: number }),
    A: new THREE.Vector3(),
    Bp: new THREE.Vector3(),
  }));
  const onBone = (bone: string, p: number[], out: THREE.Vector3) => {
    out
      .set(p[0] - BIND[bone].x, p[1] - BIND[bone].y, p[2] - BIND[bone].z)
      .applyMatrix4(B[bone].matrixWorld);
    return mesh!.worldToLocal(out);
  };
  const _ab = new THREE.Vector3();
  const _cp = new THREE.Vector3();
  const _nrm = new THREE.Vector3();
  /** Depth of a sphere inside the body, the outward normal in _nrm. */
  function inside(p: THREE.Vector3, rs: number) {
    let best = 0;
    for (const c of BODY) {
      _ab.subVectors(c.Bp, c.A);
      const tt = clamp01(_cp.subVectors(p, c.A).dot(_ab) / Math.max(1e-6, _ab.lengthSq()));
      _cp.copy(c.A).addScaledVector(_ab, tt);
      const d = _cp.subVectors(p, _cp);
      const depth = c.r + rs - d.length();
      if (depth > best) {
        best = depth;
        _nrm.copy(d).normalize();
      }
    }
    return best;
  }
  // Arm samples: [from, to, share along, radius, how much a push at the
  // wrist moves this point].
  const SAMPLES: [string, string, number, number, number][] = [
    ['ひじ', 'ひじ', 0, 0.35, 0.3],
    ['ひじ', '手首', 0.5, 0.32, 0.6],
    ['手首', '手首', 0, 0.32, 1],
    ['中指１', '中指１', 0, 0.33, 1],
    ['中指３先', '中指３先', 0, 0.22, 1],
  ];
  const _sp = new THREE.Vector3();
  const _push = new THREE.Vector3();
  /** IK to T, then out of the body: up to six pushes along its normal. */
  function reach(side: string, S: THREE.Vector3, T: THREE.Vector3, P: THREE.Vector3) {
    for (let it = 0; it < 6; it++) {
      solveArm(side, S, T, P);
      let worst = 0;
      let gain = 1;
      for (const [a, b, u, rs, g] of SAMPLES) {
        _sp.copy(at(side + a)).lerp(at(side + b), u);
        const dpt = inside(_sp, rs);
        if (dpt > worst) {
          worst = dpt;
          gain = g;
          _push.copy(_nrm);
        }
      }
      if (worst < 0.02) break;
      T.addScaledVector(_push, (worst + 0.04) / gain);
      P.addScaledVector(_push, worst + 0.04);
    }
  }

  // Colliders for the cloth springs (world space).
  const wind = new THREE.Vector3();
  const colliders: Collider[] = [];
  const COLL = [
    ['下半身', null, 1.5],
    // The seat: buttock skin reaches 1.27-1.34 from these bones.
    ['Buttock_L', null, 1.2],
    ['Buttock_R', null, 1.2],
    ['左足D', '左ひざD', 1.15],
    ['右足D', '右ひざD', 1.15],
    ['左ひざD', '左足首D', 0.7],
    ['右ひざD', '右足首D', 0.7],
    ['上半身', '首', 1.5],
    ['左腕', '左ひじ', 0.45],
    ['右腕', '右ひじ', 0.45],
    ['左ひじ', '左手首', 0.38],
    ['右ひじ', '右手首', 0.38],
    ['左手首', '左中指１', 0.45],
    ['右手首', '右中指１', 0.45],
    // The skull, behind the face so the fringe is not pushed over it.
    ['頭', null, 1.4],
  ].map(([ra, rb, r]) => ({
    ra: ra as string,
    rb: rb as string | null,
    c: { a: new THREE.Vector3(), b: rb ? new THREE.Vector3() : null, r: r as number } as Collider,
  }));
  const headUp = new THREE.Vector3(0, 1.2, -0.35);
  function placeColliders() {
    colliders.length = 0;
    for (const { ra, rb, c } of COLL) {
      c.a.setFromMatrixPosition(B[ra].matrixWorld);
      if (ra === '頭') c.a.add(_off.copy(headUp).applyQuaternion(mesh!.quaternion));
      if (rb && c.b) c.b.setFromMatrixPosition(B[rb].matrixWorld);
      colliders.push(c);
    }
  }

  // Idle life: the weight from one leg to the other every 4-9 s, glances
  // when the cursor is still, eyes that dart on their own, blinks.
  const life = {
    weight: 1,
    w: 1,
    nextShift: 3,
    glance: { x: 0, y: 0 },
    gx: 0,
    gy: 0,
    nextGlance: 5,
    eye: { x: 0, y: 0 },
    ex: 0,
    ey: 0,
    nextEye: 1,
    blinkAt: 2.5,
  };

  // Clips: one now and then, faded in and out over the procedural pose.
  const loaded: Record<string, Promise<Clip | null> | undefined> = {};
  const clipOf = (name: string) =>
    (loaded[name] ??= loadClip(`${opts.base}${MOTIONS}${CLIPS[name].file}.vrma`, (n) =>
      BIND[n].clone(),
    ).catch(() => null));
  const play = { clip: null as Clip | null, name: '', t0: 0, w: 0, next: 6 };
  async function playClip(name: string) {
    if (!mesh || walk.state !== 'here' || still) return false;
    const c = await clipOf(name);
    if (!c || disposed || walk.state !== 'here') return false;
    Object.assign(play, { clip: c, name, t0: clock.getElapsedTime() - (CLIPS[name].from ?? 0) });
    return true;
  }
  function applyClip(now: number, dt: number) {
    if (!play.clip) return;
    const u = now - play.t0;
    const len = play.clip.duration;
    // In over about 0.5 s, out over the last 0.6 s.
    play.w += ((u < len - 0.6 ? 1 : 0) - play.w) * Math.min(1, dt * 6);
    if (u >= len) {
      play.clip = null;
      play.w = 0;
      play.next = now + rnd(8, 20);
      return;
    }
    // Covering herself wins over a clip.
    const w = smooth(clamp01(play.w)) * (1 - Math.max(touch.coverW, touch.hugW));
    const p = play.clip.sample(Math.min(u, len));
    for (const [n, qq] of p.rotations) B[n]?.quaternion.slerp(qq, w);
    // She keeps to her corner: sideways steps of a clip are cut to a third.
    _off.copy(p.offset);
    _off.x *= 0.3;
    B['センター'].position.lerp(_off.add(baseP['センター']), w);
    for (const n of LEG) B[n + 'D'].quaternion.copy(B[n].quaternion);
  }
  // A clip may lift both feet off the floor (a pose struck in a jump, a
  // sit without a chair): while one plays, she is moved down (or up) until
  // her lowest point rests on the ledge. The points: the ankles and toes at
  // their bind height, the knees and the seat with a little flesh round them.
  const CONTACTS: [string, number | null][] = [
    ['左足首D', null],
    ['右足首D', null],
    ['左足先EX', null],
    ['右足先EX', null],
    ['左ひざD', 0.6],
    ['右ひざD', 0.6],
    ['Buttock_L', 1.0],
    ['Buttock_R', 1.0],
  ];
  function ground(m: THREE.SkinnedMesh) {
    if (!play.clip || play.w <= 0.001) return;
    m.updateMatrixWorld(true);
    // The floor in the model's space: the feet at their bind height.
    const floor = Math.min(BIND['左足首D'].y, BIND['右足首D'].y);
    let low = Infinity;
    for (const [n, r] of CONTACTS) {
      if (!B[n]) continue;
      // How high above the floor this point puts it.
      const h = at(n).y - (r == null ? BIND[n].y - floor : r);
      low = Math.min(low, h);
    }
    if (!Number.isFinite(low)) return;
    B['センター'].position.y -= (low - floor) * smooth(clamp01(play.w));
  }
  function direct(now: number) {
    if (now > life.nextShift) {
      life.weight = -life.weight;
      life.nextShift = now + rnd(4, 9);
    }
    if (now > life.nextGlance) {
      const look = Math.random() < 0.4;
      life.glance = look ? { x: rnd(-0.8, 0.8), y: rnd(-0.3, 0.2) } : { x: 0, y: 0 };
      life.nextGlance = now + (look ? rnd(1.2, 2.5) : rnd(3, 7));
    }
    if (now > life.nextEye) {
      life.eye = { x: rnd(-1, 1), y: rnd(-0.6, 0.5) };
      life.nextEye = now + rnd(0.6, 2.8);
    }
    if (
      !play.clip &&
      now > play.next &&
      now - typedAt > 3 &&
      now > touch.coverUntil &&
      now > touch.hugUntil
    ) {
      // Every other time or so she strikes a pose instead of a move.
      const pool = (Math.random() < 0.5 ? POSES : EMOTES).filter((n) => n !== play.name);
      play.next = now + 1e9; // set again when the clip ends
      void playClip(pool[Math.floor(Math.random() * pool.length)]).then((ok) => {
        if (!ok) play.next = now + 10;
      });
    }
  }

  // Soft tissue: the chest and seat bones (Breast_L/R, Buttock_L/R; the PMX
  // physics is not run). Each is a damped point mass trailing the place its
  // parent puts it, moved by translating the bone. The chest: about 3.5 Hz,
  // light damping, offset capped at 0.22. The seat is firmer (about 4.2 Hz,
  // more damping, capped at 0.12), so the lace over it keeps up.
  const CHEST = { k: 480, c: 9, max: 0.22 };
  const SEAT = { k: 700, c: 14, max: 0.12 };
  type Jig = typeof CHEST;
  const soft: { n: string; j: Jig; p: THREE.Vector3 | null; v: THREE.Vector3 }[] = [];
  const _tw = new THREE.Vector3();
  const _acc = new THREE.Vector3();
  const _pinv = new THREE.Quaternion();
  function jiggle(dt: number) {
    if (still) return;
    if (!soft.length)
      for (const n of ['Breast_L', 'Breast_R', 'Buttock_L', 'Buttock_R'])
        if (B[n])
          soft.push({
            n,
            j: n.startsWith('Buttock') ? SEAT : CHEST,
            p: null,
            v: new THREE.Vector3(),
          });
    for (const s of soft) {
      const bone = B[s.n];
      _tw.copy(baseP[s.n]).applyMatrix4(bone.parent!.matrixWorld);
      if (!s.p || settle || dt <= 0) {
        s.p = _tw.clone();
        s.v.set(0, 0, 0);
      } else {
        // Two substeps keep the spring stable at low frame rates.
        const h = dt / 2;
        for (let i = 0; i < 2; i++) {
          _acc.subVectors(_tw, s.p).multiplyScalar(s.j.k).addScaledVector(s.v, -s.j.c);
          s.v.addScaledVector(_acc, h);
          s.p.addScaledVector(s.v, h);
        }
        const off = _off.subVectors(s.p, _tw);
        if (off.length() > s.j.max) s.p.copy(_tw).add(off.setLength(s.j.max));
      }
      bone.parent!.getWorldQuaternion(_pinv).invert();
      _off.subVectors(s.p, _tw).applyQuaternion(_pinv);
      bone.position.copy(baseP[s.n]).add(_off);
      bone.updateMatrixWorld(true);
    }
  }

  let lastT = 0;
  let face = FACE;
  // What a touch set off: a face, a lean into a pat, a glance, a sulk.
  const touch = {
    feel: 'happy' as Feeling,
    feelUntil: -10,
    feelW: 0,
    tiltSide: 0,
    tiltUntil: -10,
    tiltW: 0,
    glance: { x: 0, y: 0 },
    glanceUntil: -10,
    sulkUntil: -10,
    coverUntil: -10,
    coverW: 0,
    hugUntil: -10,
    hugW: 0,
    blushUntil: -10,
    flinchAt: -10,
    flinchW: 0,
    mood: 0 as -1 | 0 | 1,
  };
  // Late at night (23:00 to 5:00 here) she is sleepy: slow, heavy blinks and
  // a yawn now and then. The hour is read every half minute.
  let sleepy = false;
  let hourAt = -100;
  let yawnAt = 0;
  // How far in front of her the screen is, for the angle to a point on it.
  const DEPTH = 30;
  function frame(m: THREE.SkinnedMesh) {
    const now = clock.getElapsedTime();
    const t = still ? 0 : now;
    const dt = Math.min(0.05, now - lastT);
    lastT = now;

    // Her ledge, read again: the page may have moved.
    const ft = opts.feet();
    root.position.set(ft.x * S, (VH - ft.y) * S, 0);

    // What she looks at: the end of the typed text, the cursor while it
    // moves and is not over her, or nothing (ahead). From her head, as last
    // drawn, to the target on the screen plane DEPTH units in front of her.
    const headW = m.localToWorld(at('頭').add(_off.set(0, 1.2, 0)));
    const hx = headW.x / S;
    const hy = VH - headW.y / S;
    opts.onHead?.(hx, hy);
    head.x = hx;
    head.y = hy;
    if (now - hourAt > 30) {
      hourAt = now;
      const h = new Date().getHours();
      sleepy = h >= 23 || h < 5;
    }
    const field = opts.field?.() ?? null;
    const typing = field != null && document.activeElement === field && now - typedAt < 8;
    const onHer = Math.abs(pointer.x - hx) < 70 && pointer.y > hy - 70 && pointer.y < ft.y;
    const sulking = now < touch.sulkUntil;
    const target = sulking
      ? null
      : now < touch.glanceUntil
        ? touch.glance
        : typing
          ? caret(field)
          : now - pointer.at < STILL && pointer.x >= 0 && !onHer
            ? pointer
            : null;
    let lookYaw = 0;
    let lookPitch = 0;
    if (target) {
      const dx = (target.x - hx) * S;
      const dy = (hy - target.y) * S;
      lookYaw = Math.atan2(dx, DEPTH);
      lookPitch = Math.atan2(dy, Math.hypot(dx, DEPTH));
    }
    // The body takes most of a turn, unless a clip leads.
    const faceWant = sulking
      ? FACE + SULK
      : target && !play.clip
        ? THREE.MathUtils.clamp(lookYaw * 0.85, -1.25, 1.25)
        : FACE;
    face += (faceWant - face) * Math.min(1, dt * 2.5);

    // Where she stands and which way she faces.
    if (walk.state === 'here') {
      if (!spin.drag) {
        spin.yaw += spin.vel;
        spin.vel *= Math.pow(0.04, dt);
        if (now - spin.idleAt > 2.5) {
          spin.yaw = Math.atan2(Math.sin(spin.yaw), Math.cos(spin.yaw));
          spin.yaw -= spin.yaw * Math.min(1, dt * 2.2);
        }
      }
      m.rotation.y = face + spin.yaw;
    }
    let lift = 0;
    if (walk.state === 'leaving' || walk.state === 'arriving') {
      const leaving = walk.state === 'leaving';
      const u = now - walk.t0;
      let op: number;
      if (leaving) op = 1 - smooth(clamp01((u - (LEAVE - FADE_OUT)) / FADE_OUT));
      else {
        op = smooth(clamp01(u / FADE_IN));
        lift = -RISE * (1 - smooth(clamp01(u / ARRIVE)));
      }
      // Frame by frame here, not by the stylesheet's slow fade-in.
      canvas.style.transition = 'none';
      canvas.style.opacity = String(op);
      m.rotation.y = FACE;
      m.visible = true;
      if (u > (leaving ? LEAVE : ARRIVE)) {
        walk.state = leaving ? 'away' : 'here';
        m.visible = !leaving;
        canvas.style.opacity = '';
        canvas.style.transition = '';
        lift = 0;
        spin.yaw = spin.vel = 0;
        const done = walk.done;
        walk.done = undefined;
        done?.();
        // Back in her corner: a hello.
        if (!leaving) void playClip('greet');
      }
    }
    const idle = walk.state === 'here' && !still;
    if (idle) direct(now);

    // Weight from one leg to the other, eased (about 1.2 s).
    life.w += (life.weight - life.w) * Math.min(1, dt * 2.5);
    const wgt = idle ? life.w : 0; // +1: on her left leg
    const br = Math.sin(t * 1.5) * 0.5 + 0.5; // breath, 0..1

    // Hips over the standing leg, a little down.
    move('センター', wgt * 0.4, -0.18 - Math.abs(wgt) * 0.08 + lift, 0);
    pose('下半身', 0, 0, -wgt * 0.1);
    pose('上半身', -0.012 * br, Math.sin(t * 0.33) * 0.05, wgt * 0.06 + Math.sin(t * 0.5) * 0.012);
    pose('上半身2', -0.015 * br, 0, wgt * 0.025);
    // Shoulders: up a little on every breath and over the free leg.
    for (const [side, sg] of [
      ['左', 1],
      ['右', -1],
    ] as const)
      pose(side + '肩', 0, 0, sg * (0.035 * br + Math.max(0, -sg * wgt) * 0.03));

    // Feet through IK, in the model's own space (forward is +z): the free
    // foot slides a little forward and in, heel up. They rise with her.
    for (const [side, sgn] of [
      ['左', 1],
      ['右', -1],
    ] as const) {
      const f = smooth(clamp01(sgn * -wgt));
      move(side + '足ＩＫ', -sgn * 0.25 * f, 0.25 * f + lift, 0.55 * f);
    }
    for (const n of LEG) B[n].quaternion.copy(baseQ[n]);
    m.updateMatrixWorld(true);
    ik?.update();
    // The skin and the colliders hang on the D bones, copies of the plain
    // chain by grant.
    for (const n of LEG) B[n + 'D'].quaternion.copy(B[n].quaternion);

    // Arms: the wrists go to a point on the skirt's flare (by the thighs in
    // the slip and the lingerie) by two-bone IK, kept out of the body.
    const W = at('下半身');
    // Covering: one hand over her chest, the other low in front.
    touch.coverW += ((now < touch.coverUntil ? 1 : 0) - touch.coverW) * Math.min(1, dt * 5);
    blushU.value += ((now < touch.blushUntil ? 0.7 : 0) - blushU.value) * Math.min(1, dt * 1.5);
    const cw = smooth(clamp01(touch.coverW));
    touch.hugW += ((now < touch.hugUntil ? 1 : 0) - touch.hugW) * Math.min(1, dt * 4);
    const hw = smooth(clamp01(touch.hugW));
    for (const c of BODY) {
      onBone(c.a[0], c.a[1], c.A);
      onBone(c.b[0], c.b[1], c.Bp);
    }
    for (const [side, sd] of [
      ['左', 1],
      ['右', -1],
    ] as const) {
      const sway = Math.sin(t * 0.9 + sd) * 0.025 + life.gx * 0.02;
      pose(side + '腕', -0.12 - wgt * sd * 0.03 + sway, 0, -sd * (0.72 + br * 0.02));
      B[side + '腕捩'].quaternion.copy(baseQ[side + '腕捩']);
      turn(side + 'ひじ', AX[side].flex, 0.35 + Math.sin(t * 0.7 + sd) * 0.04);
      pose(side + '手首', 0, 0, sd * 0.08);
      const S = at(side + '腕');
      const bob = Math.sin(t * 0.9 + sd) * 0.05;
      const T =
        outfit === 'p1'
          ? W.clone().add(_off.set(sd * 2.6, -1.3 + bob, 0.9))
          : W.clone().add(_off.set(sd * 2.75, -1.75 + bob, 0.3));
      const Pl = S.clone().add(_off.set(sd * 2, -2, -2.5));
      if (cw > 0.001) {
        // Her right hand over her chest, her left low in front of her hips.
        const C =
          sd < 0
            ? at('上半身2').add(_off.set(0.35, 0.75, 1.05))
            : W.clone().add(_off.set(-0.2, -0.85, 1.25));
        T.lerp(C, cw);
        Pl.lerp(S.clone().add(_off.set(sd * 2.2, -1.2, -0.8)), cw);
      }
      if (hw > 0.001) {
        // Hugging herself: each hand on the other arm, below the shoulder.
        const other = sd > 0 ? '右' : '左';
        const C = at(other + '腕')
          .lerp(at(other + 'ひじ'), 0.7)
          .add(_off.set(0, -0.1, 0.6));
        T.lerp(C, hw);
        Pl.lerp(S.clone().add(_off.set(sd * 1.5, -2.2, 0.6)), hw);
      }
      reach(side, S, T, Pl);
    }
    for (const g of grants) g.bone.quaternion.copy(_id).slerp(g.from.quaternion, g.ratio);

    // Head and eyes: what the body did not turn to; ahead with nothing to watch.
    const follow = walk.state === 'here' ? 1 : 0;
    const rel = Math.atan2(Math.sin(lookYaw - m.rotation.y), Math.cos(lookYaw - m.rotation.y));
    const tx = target ? THREE.MathUtils.clamp(rel / 0.75, -1.4, 1.4) * follow : 0;
    const ty = target ? THREE.MathUtils.clamp(-lookPitch / 0.45, -1.2, 1.3) * follow : 0;
    life.gx += (tx - life.gx) * Math.min(1, dt * 3);
    life.gy += (ty - life.gy) * Math.min(1, dt * 3);
    // Speaking: the lips from the voice, and the head nods a little with
    // the stress of the words.
    const mo = voice.read(dt);
    touch.tiltW += ((now < touch.tiltUntil ? 1 : 0) - touch.tiltW) * Math.min(1, dt * 4);
    const lean = touch.tiltW * touch.tiltSide;
    // A flinch: the head jerks back and eases forward again.
    touch.flinchW += ((now - touch.flinchAt < 0.25 ? 1 : 0) - touch.flinchW) * Math.min(1, dt * 10);
    const back = touch.flinchW * 0.16;
    pose(
      '首',
      life.gy * 0.15 + touch.tiltW * 0.06 - back * 0.5,
      life.gx * 0.25,
      -wgt * 0.03 + lean * 0.1,
    );
    pose(
      '頭',
      life.gy * 0.25 + Math.sin(t * 0.7) * 0.02 + mo.open * 0.05 + touch.tiltW * 0.08 - back,
      life.gx * 0.35,
      Math.sin(t * 0.45) * 0.05 - wgt * 0.04 + lean * 0.22,
    );
    // Eyes on the target, or straight ahead.
    life.ex += (life.gx * 0.14 - life.ex) * Math.min(1, dt * 12);
    life.ey += (life.gy * 0.08 - life.ey) * Math.min(1, dt * 12);
    pose('両目', life.ey, life.ex, 0);

    // A blink every three to six seconds and a soft smile (more of one in a
    // good mood, none in a bad one). Sleepy, the blinks are slower and the
    // lids hang a little, and now and then she yawns.
    const blinkLen = sleepy ? 0.4 : 0.16;
    const d = t - life.blinkAt;
    let blink = 0;
    if (d > 0 && d < blinkLen) blink = Math.sin((d / blinkLen) * Math.PI);
    else if (d >= blinkLen) life.blinkAt = t + (sleepy ? rnd(2, 4) : rnd(3, 6));
    morph('まばたき', sleepy ? Math.max(0.22, blink) : blink);
    if (sleepy && idle && !play.clip && now > touch.feelUntil + 2 && now > yawnAt) {
      if (yawnAt > 0) {
        touch.feel = 'yawn';
        touch.feelUntil = now + 2.4;
      }
      yawnAt = now + rnd(45, 100);
    }
    const smile = touch.mood === 1 ? 0.5 : touch.mood === -1 ? 0 : 0.3;
    morph('mouthSmileLeft', smile - mo.open * 0.15);
    morph('mouthSmileRight', smile - mo.open * 0.15);
    morph('あ', mo.a * 0.8);
    morph('い', mo.i * 0.8);
    morph('う', mo.u * 0.8);
    morph('え', mo.e * 0.8);
    morph('お', mo.o * 0.8);
    // A face from a touch, eased in and out over her own.
    // Shapes only the faces use rest at 0 unless a face holds them this frame.
    touch.feelW += ((now < touch.feelUntil ? 1 : 0) - touch.feelW) * Math.min(1, dt * 6);
    for (const n of FACE_ONLY) morph(n, 0);
    // A bad mood: brows down (mostly under her fringe) and the corners of
    // the mouth too.
    if (touch.mood === -1) {
      morph('browDownLeft', 0.3);
      morph('browDownRight', 0.3);
      morph('mouthFrownLeft', 0.3);
      morph('mouthFrownRight', 0.3);
    }
    if (touch.feelW > 0.001 && m.morphTargetInfluences)
      for (const [n, v] of Object.entries(FACES[touch.feel])) {
        const i = morphs[n];
        if (i == null) continue;
        const was = m.morphTargetInfluences[i];
        m.morphTargetInfluences[i] = was + (v - was) * touch.feelW;
      }
    // Cold: a fast small shiver through the neck and shoulders.
    if (touch.hugW > 0.01) {
      const sh = Math.sin(now * 52) * 0.018 * touch.hugW;
      B['首'].rotateZ(sh);
      B['左肩'].rotateZ(sh * 0.6);
      B['右肩'].rotateZ(-sh * 0.6);
    }

    if (!still) watchDwell(now);
    applyClip(now, dt);
    ground(m);

    // Cloth, hair and the chest last, on the finished pose.
    m.updateMatrixWorld(true);
    jiggle(dt);
    if (springs) {
      if (settle) {
        springs.reset();
        settle = false;
      }
      // Under reduced motion the hair and cloth hang at rest, no wind.
      if (!still) {
        placeColliders();
        wind.set(
          Math.sin(now * 0.7) * 0.6 + Math.sin(now * 1.9) * 0.3,
          0,
          Math.sin(now * 0.5) * 0.4,
        );
        springs.update(dt, colliders, wind);
      }
    }
  }

  // Something on screen can change: a walk, a clip, a drag or a turn back,
  // speech, a touch's face or lean easing in and out, the cursor or typing
  // she follows. Otherwise she only breathes and blinks, or, under reduced
  // motion, nothing moves at all.
  function lively(now: number) {
    return (
      now < wakeUntil ||
      walk.state === 'arriving' ||
      walk.state === 'leaving' ||
      play.clip !== null ||
      spin.drag !== null ||
      Math.abs(spin.yaw) > 1e-3 ||
      Math.abs(spin.vel) > 1e-4 ||
      voice.speaking ||
      following ||
      now <
        Math.max(
          touch.feelUntil,
          touch.tiltUntil,
          touch.glanceUntil,
          touch.sulkUntil,
          touch.coverUntil,
          touch.hugUntil,
          touch.blushUntil,
        ) ||
      now - pointer.at < 1 ||
      now - typedAt < 1
    );
  }

  // Sent away she is not drawn: after a frame that clears her, the loop
  // only waits for her to be called back. (A hidden tab gets no frames.)
  // Calm (nothing above for 1.5 s), she is drawn at 30 frames a second, the
  // breath and blinks need no more; under reduced motion a calm frame is
  // not drawn at all, unless her ledge moved, and once a minute in case
  // the hour made her sleepy.
  let awayFrames = 0;
  let busyAt = -10;
  let drawnAt = -10;
  let feetAt = { x: NaN, y: NaN };
  function tick() {
    if (disposed) return;
    raf = requestAnimationFrame(tick);
    awayFrames = walk.state === 'away' ? awayFrames + 1 : 0;
    if (awayFrames >= 3) return;
    const now = clock.getElapsedTime();
    if (lively(now)) busyAt = now;
    const ft = opts.feet();
    const moved = ft.x !== feetAt.x || ft.y !== feetAt.y;
    if (mesh && !moved && now - busyAt > 1.5) {
      if (still ? now - drawnAt < 60 : now - drawnAt < 1 / 30 - 0.004) return;
    }
    feetAt = ft;
    drawnAt = now;
    if (mesh) frame(mesh);
    renderer.render(scene, cam);
  }
  const onMotion = () => {
    still = motion.matches;
    if (still) {
      // Stop where she is: no clip, no turn, the hair and cloth at rest.
      play.clip = null;
      play.w = 0;
      spin.yaw = spin.vel = 0;
    }
    settle = true;
    wake();
  };
  motion.addEventListener('change', onMotion);
  place();
  tick();

  return {
    present(on, done) {
      wake();
      if (still || !mesh) {
        walk.state = on ? 'here' : 'away';
        if (mesh) mesh.visible = on;
        done?.();
        return;
      }
      if (on && (walk.state === 'here' || walk.state === 'arriving')) return done?.();
      if (!on && (walk.state === 'away' || walk.state === 'leaving')) return done?.();
      spin.drag = null;
      spin.yaw = spin.vel = 0;
      play.clip = null;
      play.w = 0;
      walk.state = on ? 'arriving' : 'leaving';
      walk.t0 = clock.getElapsedTime();
      walk.done = done;
      // She appears where she stands: no whip in the cloth from the jump.
      if (on) settle = true;
      // Going: a wave goodbye first.
      else
        void clipOf('greet').then((c) => {
          if (c && !disposed && walk.state === 'leaving')
            Object.assign(play, { clip: c, name: 'greet', t0: clock.getElapsedTime() - WAVE_FROM });
        });
    },
    feel(f, seconds) {
      wake();
      const now = clock.getElapsedTime();
      // A new face replaces the old one at once.
      if (f !== touch.feel) touch.feelW = 0;
      touch.feel = f;
      touch.feelUntil = now + seconds;
    },
    tilt(side, seconds) {
      wake();
      touch.tiltSide = side;
      touch.tiltUntil = clock.getElapsedTime() + seconds;
    },
    glance(x, y, seconds) {
      wake();
      touch.glance = { x, y };
      touch.glanceUntil = clock.getElapsedTime() + seconds;
    },
    sulk(seconds) {
      wake();
      touch.sulkUntil = clock.getElapsedTime() + seconds;
    },
    cover(seconds) {
      wake();
      touch.coverUntil = clock.getElapsedTime() + seconds;
    },
    hug(seconds) {
      wake();
      touch.hugUntil = clock.getElapsedTime() + seconds;
    },
    blush(seconds) {
      wake();
      touch.blushUntil = Math.max(touch.blushUntil, clock.getElapsedTime() + seconds);
    },
    act(name) {
      wake();
      if (CLIPS[name]) void playClip(name);
    },
    mood(level) {
      wake();
      touch.mood = level;
    },
    hush: () => {
      voice.stop();
      wake();
    },
    setOutfit: (o) => {
      dress(o);
      wake();
    },
    say: (url) => {
      wake();
      return voice.say(url).finally(() => wake());
    },
    follow: (stream) => {
      following = stream !== null;
      wake();
      voice.follow(stream);
    },
    dispose() {
      disposed = true;
      clearTimeout(burst.timer);
      voice.dispose();
      cancelAnimationFrame(raf);
      motion.removeEventListener('change', onMotion);
      resize.disconnect();
      removeEventListener('pointermove', onPointer);
      removeEventListener('input', onInput, true);
      hit.removeEventListener('pointerdown', onDown);
      hit.removeEventListener('pointermove', onMove);
      hit.removeEventListener('pointerup', onUp);
      hit.removeEventListener('pointercancel', onUp);
      scene.traverse((o) => {
        const mm = o as THREE.Mesh;
        mm.geometry?.dispose();
        const mats = mm.material ? (Array.isArray(mm.material) ? mm.material : [mm.material]) : [];
        for (const mat of mats) {
          for (const v of Object.values(mat)) if (v instanceof THREE.Texture) v.dispose();
          mat.dispose();
        }
      });
      renderer.dispose();
    },
  };
}
