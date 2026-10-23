import * as THREE from 'three';

/*
 * Spring bones in the manner of VRM (three-vrm's VRMSpringBoneJoint): every
 * joint keeps a tail point that follows inertia, a pull back to its rest
 * direction, gravity and the colliders, then the bone is turned to aim at
 * it. Calmer than rigid-body cloth, and every group is tunable.
 */

// The model is about 21.8 units tall for a 1.6 m girl: 13.6 units a metre.
// VRM settings are in metres, so the pulls are scaled by this.
const UNIT = 13.6;
// Up to 35 degrees: a panel follows a thigh's swing, not a full squat.
const MAX_TURN = 0.6;

export interface SpringSettings {
  stiffness: number;
  gravity: number;
  drag: number;
  radius: number;
  wind: number;
  /** Share of a nearby thigh's turn a front skirt panel takes. */
  follow?: number;
}

export interface SpringGroup {
  name: string;
  test: (bone: string) => boolean;
  settings: SpringSettings;
}

/** A sphere (b null) or a capsule, in world space. */
export interface Collider {
  a: THREE.Vector3;
  b: THREE.Vector3 | null;
  r: number;
}

export interface Leg {
  thigh: THREE.Bone;
  hips: THREE.Bone;
  /** The thigh capsule in bind model space. */
  a: THREE.Vector3;
  b: THREE.Vector3;
  r: number;
}

interface MmdRigidBody {
  type: number;
  boneIndex: number;
}

const _v = new THREE.Vector3();
const _a = new THREE.Vector3();
const _b = new THREE.Vector3();
const _ax = new THREE.Vector3();
const _q = new THREE.Quaternion();
const _pq = new THREE.Quaternion();
const _iq = new THREE.Quaternion();
const _lq = new THREE.Quaternion();
const _h = new THREE.Quaternion();
const _t = new THREE.Quaternion();
const _m = new THREE.Quaternion();

type Lead = [THREE.Quaternion, number][];

/** The weighted mean of the thighs' turns, its angle capped. */
function mixTurns(lead: Lead): THREE.Quaternion {
  let sum = 0;
  let top = 0;
  _m.identity();
  for (const [turn, w] of lead) {
    sum += w;
    top = Math.max(top, w);
    _m.slerp(turn, w / sum);
  }
  const ang = 2 * Math.acos(Math.min(1, Math.abs(_m.w)));
  if (ang > 1e-4) {
    _ax
      .set(_m.x, _m.y, _m.z)
      .normalize()
      .multiplyScalar(Math.sign(_m.w) || 1);
    _m.setFromAxisAngle(_ax, Math.min(ang, MAX_TURN) * top);
  }
  return _m;
}

/** The nearest point of a collider to p, into out. */
function nearest(c: Collider, p: THREE.Vector3, out: THREE.Vector3): THREE.Vector3 {
  if (!c.b) return out.copy(c.a);
  _a.subVectors(c.b, c.a);
  const t = THREE.MathUtils.clamp(out.subVectors(p, c.a).dot(_a) / _a.lengthSq(), 0, 1);
  return out.copy(c.a).addScaledVector(_a, t);
}

class Joint {
  readonly initQ: THREE.Quaternion;
  readonly tailLocal: THREE.Vector3;
  readonly axis: THREE.Vector3;
  readonly length: number;
  /** How deep the tail sat in each collider as made (rest-relative). */
  readonly slack = new Map<Collider, number>();
  readonly head = new THREE.Vector3();
  readonly next = new THREE.Vector3();
  readonly pq = new THREE.Quaternion();
  readonly cur = new THREE.Vector3();
  readonly prev = new THREE.Vector3();
  lead: Lead | null = null;

  constructor(
    readonly bone: THREE.Bone,
    readonly s: SpringSettings,
    readonly group: string,
    readonly depth: number,
  ) {
    this.initQ = bone.quaternion.clone();
    const child = bone.children.find((c): c is THREE.Bone => (c as THREE.Bone).isBone);
    // A leaf gets a virtual tail along its own segment.
    this.tailLocal = child ? child.position.clone() : bone.position.clone().multiplyScalar(0.6);
    this.axis = this.tailLocal.clone().normalize();
    this.length = this.tailLocal.length();
    this.reset();
  }

  private placeHead() {
    const bone = this.bone;
    bone.quaternion.copy(this.initQ);
    bone.updateMatrix();
    bone.matrixWorld.multiplyMatrices(bone.parent!.matrixWorld, bone.matrix);
  }

  reset() {
    this.placeHead();
    this.cur.copy(this.tailLocal).applyMatrix4(this.bone.matrixWorld);
    this.prev.copy(this.cur);
  }

  /** Where the tail goes this step (this.next), not yet applied. */
  predict(dt: number, colliders: Collider[], wind: THREE.Vector3) {
    const s = this.s;
    this.placeHead();
    const head = this.head.setFromMatrixPosition(this.bone.matrixWorld);
    this.pq.setFromRotationMatrix(this.bone.parent!.matrixWorld).multiply(this.initQ);
    _pq.copy(this.pq);
    // Verlet: inertia, the pull to rest, gravity and the breeze.
    const next = this.next
      .copy(this.cur)
      .sub(this.prev)
      .multiplyScalar(1 - s.drag)
      .add(this.cur);
    _a.copy(this.axis).applyQuaternion(_pq);
    // A skirt panel near a thigh turns its rest direction with that thigh,
    // so a leg swinging forward carries the panel instead of slitting it.
    if (this.lead) _a.applyQuaternion(mixTurns(this.lead));
    next.add(_a.multiplyScalar(s.stiffness * dt * UNIT));
    next.add(_a.set(0, -s.gravity * dt * UNIT, 0));
    next.addScaledVector(wind, s.wind * dt * UNIT);
    // Keep the length, then push out of the body.
    next.sub(head).setLength(this.length).add(head);
    for (const c of colliders) {
      // Rest-relative: where the cloth was made inside a collider (a fitted
      // slip over the seat), that much is allowed, so only motion pushes.
      const r = c.r + s.radius - (this.slack.get(c) ?? 0);
      nearest(c, next, _b);
      const d = _a.subVectors(next, _b);
      const len = d.length();
      if (len < r && len > 1e-6) {
        next.copy(_b).addScaledVector(d, r / len);
        next.sub(head).setLength(this.length).add(head);
      }
    }
  }

  /** Apply this.next: turn the bone from its rest axis to the tail. */
  commit() {
    const bone = this.bone;
    const next = this.next;
    const head = this.head;
    next.sub(head).setLength(this.length).add(head);
    this.prev.copy(this.cur);
    this.cur.copy(next);
    _iq.copy(this.pq).invert();
    const to = _a.subVectors(next, head).applyQuaternion(_iq).normalize();
    bone.quaternion.copy(this.initQ).multiply(_q.setFromUnitVectors(this.axis, to));
    bone.updateMatrix();
    bone.matrixWorld.multiplyMatrices(bone.parent!.matrixWorld, bone.matrix);
  }
}

interface Link {
  a: Joint;
  b: Joint;
  depth: number;
  rest: number;
  cut: boolean;
}

interface LegState extends Leg {
  rel0: THREE.Quaternion;
  turn: THREE.Quaternion;
}

export interface Springs {
  /** Groups not simulated (the outfit not worn). */
  off: Set<string>;
  reset(): void;
  calibrate(colliders: Collider[]): void;
  setLeads(legs: Leg[]): void;
  update(dt: number, colliders: Collider[], wind: THREE.Vector3): void;
}

/**
 * The spring bones of a PMX mesh: the bones the model gave physics rigid
 * bodies (type 1 or 2), so the author's choice of what moves is kept, sorted
 * into the groups by name.
 */
export function createSprings(mesh: THREE.SkinnedMesh, groups: SpringGroup[]): Springs {
  const bodies = (mesh.geometry.userData.MMD?.rigidBodies ?? []) as MmdRigidBody[];
  const bones = mesh.skeleton.bones;
  const physical = new Set(bodies.filter((r) => r.type !== 0).map((r) => r.boneIndex));
  const depthOf = (b: THREE.Object3D) => {
    let n = 0;
    for (let p = b.parent; p && (p as THREE.Bone).isBone; p = p.parent) n++;
    return n;
  };
  mesh.updateMatrixWorld(true);
  const joints: Joint[] = [];
  for (const i of physical) {
    const bone = bones[i];
    const g = bone && groups.find((x) => x.test(bone.name));
    if (g) joints.push(new Joint(bone, g.settings, g.name, depthOf(bone)));
  }
  // Parents before children, so every head is where its parent left it.
  joints.sort((a, b) => a.depth - b.depth);

  // Horizontal ties: a skirt is a ring of bone chains (P1-Skirt_<ring>_
  // <around>), and nothing held a chain to the next one, so a leg pushed two
  // apart and opened a slit. Neighbours on the same ring are tied at their
  // bind distance; a tie only resists stretching, up to 6%.
  const ring = /^(.+Skirt)_(\d+)_(\d+)$/;
  const byKey = new Map<string, Joint>();
  for (const j of joints) {
    const m = ring.exec(j.bone.name);
    if (m) byKey.set(`${m[1]}|${m[2]}|${m[3]}`, j);
  }
  const links: Link[] = [];
  for (const j of joints) {
    const m = ring.exec(j.bone.name);
    if (!m) continue;
    const around = [...byKey.keys()].filter((k) => k.startsWith(`${m[1]}|${m[2]}|`)).length;
    const n = byKey.get(`${m[1]}|${m[2]}|${(Number(m[3]) + 1) % around}`);
    if (n) links.push({ a: j, b: n, depth: j.depth, rest: j.cur.distanceTo(n.cur), cut: false });
  }
  // A ring that is not closed: drop the tie that spans the gap.
  for (const g of new Set(links.map((l) => l.a.group))) {
    const rests = links
      .filter((l) => l.a.group === g)
      .map((l) => l.rest)
      .sort((x, y) => x - y);
    const median = rests[rests.length >> 1];
    for (const l of links) if (l.a.group === g && l.rest > median * 2.2) l.cut = true;
  }
  const levels = [...new Set(joints.map((j) => j.depth))].sort((a, b) => a - b);
  let legs: LegState[] = [];

  const springs: Springs = {
    off: new Set(),
    reset() {
      mesh.updateMatrixWorld(true);
      for (const j of joints) j.reset();
    },
    // With the model in its bind pose and the colliders placed on it: how
    // deep each joint's tail already sits in each collider, as made.
    calibrate(colliders) {
      springs.reset();
      const p = new THREE.Vector3();
      for (const j of joints)
        for (const c of colliders) {
          const depth = c.r + j.s.radius - j.cur.distanceTo(nearest(c, j.cur, p));
          if (depth > 0) j.slack.set(c, depth + 0.02);
        }
    },
    // Thighs that lead nearby front panels. Call in the bind pose.
    setLeads(list) {
      springs.reset();
      legs = list.map((l) => ({
        ...l,
        rel0: l.hips
          .getWorldQuaternion(new THREE.Quaternion())
          .invert()
          .multiply(l.thigh.getWorldQuaternion(new THREE.Quaternion()))
          .invert(),
        turn: new THREE.Quaternion(),
      }));
      const p = new THREE.Vector3();
      for (const j of joints) {
        if (!j.s.follow) continue;
        mesh.worldToLocal(p.copy(j.cur));
        const lead: Lead = [];
        for (const l of legs) {
          const d = p.distanceTo(nearest({ a: l.a, b: l.b, r: l.r }, p, _b)) - l.r;
          // Only the front and the sides (the back hangs over the seat), and
          // only panels over this thigh: the middle is held by the colliders,
          // so it neither lifts nor splits. Full share within 0.3 of the
          // thigh, none from 1.8 away.
          const front = THREE.MathUtils.clamp((p.z + 0.4) / 0.8, 0, 1);
          const over =
            Math.sign(p.x) === Math.sign(l.a.x)
              ? THREE.MathUtils.clamp((Math.abs(p.x) - 0.35) / 0.6, 0, 1)
              : 0;
          const w = j.s.follow * front * over * THREE.MathUtils.clamp(1 - (d - 0.3) / 1.5, 0, 1);
          if (w > 0.02) lead.push([l.turn, w]);
        }
        if (lead.length) j.lead = lead;
      }
    },
    update(dt, colliders, wind) {
      // The thighs' turn relative to the hips since the bind pose, in world.
      for (const l of legs) {
        l.hips.getWorldQuaternion(_h);
        l.thigh.getWorldQuaternion(_t);
        l.turn.copy(_h).invert().multiply(_t).multiply(l.rel0);
        l.turn.premultiply(_h).multiply(_lq.copy(_h).invert());
      }
      const on = (j: Joint) => !springs.off.has(j.group);
      for (const d of levels) {
        const level = joints.filter((j) => j.depth === d && on(j));
        for (const j of level) j.predict(dt, colliders, wind);
        // Pull stretched neighbours back together, a few passes.
        for (let it = 0; it < 3; it++)
          for (const l of links) {
            if (l.depth !== d || l.cut || !on(l.a)) continue;
            const v = _v.subVectors(l.b.next, l.a.next);
            const len = v.length();
            const max = l.rest * 1.06;
            if (len <= max || len < 1e-6) continue;
            v.multiplyScalar(((len - max) / len) * 0.5);
            l.a.next.add(v);
            l.b.next.sub(v);
          }
        for (const j of level) j.commit();
      }
    },
  };
  return springs;
}
