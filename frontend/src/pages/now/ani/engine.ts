import * as THREE from 'three';
import { CCDIKSolver, type IK } from 'three/examples/jsm/animation/CCDIKSolver.js';
import { MMDLoader } from 'three/examples/jsm/loaders/MMDLoader.js';

import { loadClip, type Clip } from './motion';
import { createSprings, type Collider, type SpringGroup, type Springs } from './springs';
import { createVoice } from './voice';

/*
 * Ani on Now: the PMX model, posed every frame by a small director (weight
 * from one leg to the other, glances, blinks), the VRoid VRMA clips and the
 * Pic♡Pix standing poses over it, spring bones for the skirt, hair and
 * ribbons, a damped jiggle for the chest and seat, and the arms kept out of
 * the body.
 *
 * The body is v2/Ani.pmx, in lingerie. The dress and the slip come from
 * clothes/Ani.pmx, the first model on the same skeleton (the same 628 bones in
 * the same order at the same places): its clothes are skinned to the v2
 * skeleton as they are.
 */

export type Outfit = 'p1' | 'p2' | 'lingerie';

export interface AniOptions {
  /** Folder of v1/ and v2/ (ends with a slash). */
  base: string;
  /** The element that takes drags and taps (the canvas lets them through). */
  hit: HTMLElement;
  /** Where her centre sits, in CSS px from the canvas's left edge. */
  anchor: number;
  outfit: Outfit;
  present: boolean;
  onReady?: () => void;
  onError?: (e: unknown) => void;
  /** A tap on her (not a drag). */
  onTap?: () => void;
  /** A long drag that spun her round more than once. */
  onSpin?: () => void;
}

export interface Ani {
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

// Camera framing in model units (the PMX is about 21.8 tall).
const FRAME = { fov: 22, dist: 57, y: 11 };
const FACE = 0.25;
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
const EMOTES = ['show', 'peace', 'spin', 'pose', 'squat', 'shoot'];
// Standing poses from the Pic♡Pix Free Pose set (Unity humanoid poses baked
// to VRMA, each held for 6 s), under poses/pose<n>.
const POSES: string[] = [];
for (let n = 1; n <= 30; n++) {
  POSES.push(`pose${n}`);
  CLIPS[`pose${n}`] = { file: `poses/pose${n}` };
}

// Files under the base.
const BODY_PMX = 'v2/Ani.pmx';
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
  const still = matchMedia('(prefers-reduced-motion: reduce)').matches;
  const renderer = new THREE.WebGLRenderer({ canvas, alpha: true, antialias: true });
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
  // The toon materials read right with linear output.
  renderer.outputColorSpace = THREE.LinearSRGBColorSpace;
  const scene = new THREE.Scene();
  const cam = new THREE.PerspectiveCamera(FRAME.fov, 1, 1, 200);
  scene.add(new THREE.AmbientLight(0xffffff, 0.9));
  const key = new THREE.DirectionalLight(0xffe2d6, 1.1);
  key.position.set(-6, 12, 10);
  scene.add(key);
  const rim = new THREE.DirectionalLight(0x9fb8ff, 0.9);
  rim.position.set(8, 10, -6);
  scene.add(rim);

  const place = () => {
    const w = canvas.clientWidth;
    const h = canvas.clientHeight;
    if (!w || !h) return;
    renderer.setSize(w, h, false);
    const ax = opts.anchor;
    // A symmetric frustum twice as wide as the far side, viewed off centre.
    const full = 2 * Math.max(ax, w - ax);
    cam.aspect = full / h;
    cam.setViewOffset(full, h, full / 2 - ax, 0, w, h);
    cam.position.set(0, FRAME.y, FRAME.dist);
    cam.lookAt(0, FRAME.y, 0);
    cam.updateProjectionMatrix();
  };
  const resize = new ResizeObserver(place);
  resize.observe(canvas);

  const clock = new THREE.Clock();
  const voice = createVoice();
  let disposed = false;
  let raf = 0;
  let mesh: THREE.SkinnedMesh | null = null;
  let ik: CCDIKSolver | null = null;
  let springs: Springs | null = null;
  let settle = false;
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

  // The head follows the cursor while it moves.
  const pointer = { x: 0, y: 0, at: -10 };
  const onPointer = (e: PointerEvent) => {
    const r = canvas.getBoundingClientRect();
    pointer.x = THREE.MathUtils.clamp((e.clientX - (r.left + opts.anchor)) / (r.width * 4), -1, 1);
    pointer.y = THREE.MathUtils.clamp(
      (e.clientY - (r.top + r.height * 0.15)) / (r.height * 3),
      -1,
      1,
    );
    pointer.at = clock.getElapsedTime();
  };
  addEventListener('pointermove', onPointer);

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
  const onUp = () => {
    if (!spin.drag) return;
    if (spin.drag.moved < 5) {
      spin.vel = 0;
      if (!play.clip) void playClip('greet');
      opts.onTap?.();
    } else if (spin.drag.moved * 0.012 > Math.PI * 2) opts.onSpin?.();
    spin.drag = null;
    spin.idleAt = clock.getElapsedTime();
    delete hit.dataset.dragging;
  };
  hit.addEventListener('pointerdown', onDown);
  hit.addEventListener('pointermove', onMove);
  hit.addEventListener('pointerup', onUp);
  hit.addEventListener('pointercancel', onUp);

  // The files are PNG where the PMX names BMP textures.
  const manager = new THREE.LoadingManager();
  manager.setURLModifier((u) => u.replace(/\.bmp$/i, '.png'));
  const loader = new MMDLoader(manager);
  const load = (file: string) =>
    new Promise<THREE.SkinnedMesh>((ok, fail) =>
      loader.load(`${opts.base}${file}`, ok, undefined, fail),
    );
  Promise.all([load(BODY_PMX), load(CLOTHES_PMX)]).then(
    ([m, v1]) => {
      if (disposed) return;
      setUp(m, v1);
      opts.onReady?.();
    },
    (e) => opts.onError?.(e),
  );

  function setUp(m: THREE.SkinnedMesh, v1: THREE.SkinnedMesh) {
    mesh = m;
    m.rotation.y = FACE;
    m.visible = walk.state === 'here';
    scene.add(m);
    for (const b of m.skeleton.bones) {
      B[b.name] = b;
      baseQ[b.name] = b.quaternion.clone();
      baseP[b.name] = b.position.clone();
    }
    morphs = m.morphTargetDictionary ?? {};
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
    if (!still) {
      // v2 has rigid bodies for its hair and chest only; the clothes' bones
      // have theirs in v1.
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
    }
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
        mat.visible = o !== 'lingerie';
        mat.opacity = 1;
      }
      if (P2.includes(mat.name)) {
        mat.visible = o === 'p2';
        mat.opacity = o === 'p2' ? 1 : 0;
      }
    }
    for (const mat of mesh.material as THREE.Material[])
      if (LINGERIE.includes(mat.name)) mat.visible = o === 'lingerie';
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
    const w = smooth(clamp01(play.w));
    const p = play.clip.sample(Math.min(u, len));
    for (const [n, qq] of p.rotations) B[n]?.quaternion.slerp(qq, w);
    // She keeps to her corner: sideways steps of a clip are cut to a third.
    _off.copy(p.offset);
    _off.x *= 0.3;
    B['センター'].position.lerp(_off.add(baseP['センター']), w);
    for (const n of LEG) B[n + 'D'].quaternion.copy(B[n].quaternion);
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
    if (!play.clip && now > play.next) {
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
  function frame(m: THREE.SkinnedMesh) {
    const now = clock.getElapsedTime();
    const t = still ? 0 : now;
    const dt = Math.min(0.05, now - lastT);
    lastT = now;

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
      m.rotation.y = FACE + spin.yaw;
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
      reach(side, S, T, S.clone().add(_off.set(sd * 2, -2, -2.5)));
    }
    for (const g of grants) g.bone.quaternion.copy(_id).slerp(g.from.quaternion, g.ratio);

    // Head: the cursor while it moves, otherwise her own glances.
    const follow = walk.state === 'here' ? 1 : 0;
    const fresh = now - pointer.at < 4;
    const tx = (fresh ? pointer.x : life.glance.x) * follow;
    const ty = (fresh ? pointer.y : life.glance.y) * follow;
    life.gx += (tx - life.gx) * Math.min(1, dt * 2.4);
    life.gy += (ty - life.gy) * Math.min(1, dt * 2.4);
    // Speaking: the lips from the voice, and the head nods a little with
    // the stress of the words.
    const mo = voice.read(dt);
    pose('首', life.gy * 0.15, life.gx * 0.25, -wgt * 0.03);
    pose(
      '頭',
      life.gy * 0.25 + Math.sin(t * 0.7) * 0.02 + mo.open * 0.05,
      life.gx * 0.35,
      Math.sin(t * 0.45) * 0.05 - wgt * 0.04,
    );
    life.ex += (life.eye.x * 0.12 + life.gx * 0.1 - life.ex) * Math.min(1, dt * 12);
    life.ey += (life.eye.y * 0.08 + life.gy * 0.06 - life.ey) * Math.min(1, dt * 12);
    pose('両目', life.ey, life.ex, 0);

    // A blink every three to six seconds and a soft smile.
    const d = t - life.blinkAt;
    let blink = 0;
    if (d > 0 && d < 0.16) blink = Math.sin((d / 0.16) * Math.PI);
    else if (d >= 0.16) life.blinkAt = t + rnd(3, 6);
    morph('まばたき', blink);
    morph('mouthSmileLeft', 0.3 - mo.open * 0.15);
    morph('mouthSmileRight', 0.3 - mo.open * 0.15);
    morph('あ', mo.a * 0.8);
    morph('い', mo.i * 0.8);
    morph('う', mo.u * 0.8);
    morph('え', mo.e * 0.8);
    morph('お', mo.o * 0.8);

    applyClip(now, dt);

    // Cloth, hair and the chest last, on the finished pose.
    m.updateMatrixWorld(true);
    jiggle(dt);
    if (springs) {
      if (settle) {
        springs.reset();
        settle = false;
      }
      placeColliders();
      wind.set(Math.sin(now * 0.7) * 0.6 + Math.sin(now * 1.9) * 0.3, 0, Math.sin(now * 0.5) * 0.4);
      springs.update(dt, colliders, wind);
    }
  }

  function tick() {
    if (disposed) return;
    if (mesh) frame(mesh);
    renderer.render(scene, cam);
    raf = requestAnimationFrame(tick);
  }
  place();
  tick();

  return {
    present(on, done) {
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
    setOutfit: dress,
    say: (url) => voice.say(url),
    follow: (stream) => voice.follow(stream),
    dispose() {
      disposed = true;
      voice.dispose();
      cancelAnimationFrame(raf);
      resize.disconnect();
      removeEventListener('pointermove', onPointer);
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
