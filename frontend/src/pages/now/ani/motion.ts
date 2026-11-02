import * as THREE from 'three';
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js';
import { MMDLoader } from 'three/examples/jsm/loaders/MMDLoader.js';

/*
 * Motion clips played on the MMD skeleton: VRM Animations (.vrma), retargeted
 * from a humanoid, and plain VMD files made for this very model (loadVmdClip
 * at the end).
 *
 * A VRMA clip animates a humanoid whose rest pose is a T-pose. Some files
 * rest at identity rotations, others (exported from Blender) carry rest
 * rotations under a rotated root; as three-vrm does, a clip rotation q is
 * normalized to parentRestWorld * q * restWorld^-1: a turn relative to a
 * world-aligned rest. The MMD bones also rest at identity, world-aligned,
 * but in an A-pose: the arms slope about 35 degrees down, the fingers and
 * thumbs follow the hands. For each bone C turns the clip's rest direction
 * onto the model's, and a clip rotation N lands on the model as
 * C(parent) * N * C(bone)^-1.
 */

/** Humanoid bone -> [MMD bone, MMD child for the rest direction]. */
const MAP: Record<string, [string, string?]> = {
  hips: ['腰'],
  spine: ['上半身'],
  chest: ['上半身2'],
  neck: ['首'],
  head: ['頭'],
};
/** The clip's rest direction of a bone: towards this humanoid child. */
const CHILD: Record<string, string> = {};
/** A bone without its own direction takes its parent's correction. */
const INHERIT: Record<string, string | null> = {};
const PARENT: Record<string, string> = {
  spine: 'hips',
  chest: 'spine',
  upperChest: 'chest',
  neck: 'upperChest',
  head: 'neck',
};
const FINGERS: [string, string][] = [
  ['Index', '人指'],
  ['Middle', '中指'],
  ['Ring', '薬指'],
  ['Little', '小指'],
];
for (const [s, j] of [
  ['left', '左'],
  ['right', '右'],
] as const) {
  Object.assign(MAP, {
    [s + 'Shoulder']: [j + '肩', j + '腕'],
    [s + 'UpperArm']: [j + '腕', j + 'ひじ'],
    [s + 'LowerArm']: [j + 'ひじ', j + '手首'],
    [s + 'Hand']: [j + '手首', j + '中指１'],
    [s + 'ThumbMetacarpal']: [j + '親指０', j + '親指１'],
    [s + 'ThumbProximal']: [j + '親指１', j + '親指２'],
    [s + 'ThumbDistal']: [j + '親指２', j + '親指２先'],
    [s + 'UpperLeg']: [j + '足', j + 'ひざ'],
    [s + 'LowerLeg']: [j + 'ひざ', j + '足首'],
    [s + 'Foot']: [j + '足首', j + '足先EX'],
  });
  Object.assign(CHILD, {
    [s + 'Shoulder']: s + 'UpperArm',
    [s + 'UpperArm']: s + 'LowerArm',
    [s + 'LowerArm']: s + 'Hand',
    [s + 'Hand']: s + 'MiddleProximal',
    [s + 'ThumbMetacarpal']: s + 'ThumbProximal',
    [s + 'ThumbProximal']: s + 'ThumbDistal',
    [s + 'UpperLeg']: s + 'LowerLeg',
    [s + 'LowerLeg']: s + 'Foot',
  });
  INHERIT[s + 'ThumbDistal'] = s + 'ThumbProximal';
  INHERIT[s + 'Foot'] = null; // the ankle keeps the clip's turn as is
  Object.assign(PARENT, {
    [s + 'Shoulder']: 'upperChest',
    [s + 'UpperArm']: s + 'Shoulder',
    [s + 'LowerArm']: s + 'UpperArm',
    [s + 'Hand']: s + 'LowerArm',
    [s + 'UpperLeg']: 'hips',
    [s + 'LowerLeg']: s + 'UpperLeg',
    [s + 'Foot']: s + 'LowerLeg',
    [s + 'ThumbMetacarpal']: s + 'Hand',
    [s + 'ThumbProximal']: s + 'ThumbMetacarpal',
    [s + 'ThumbDistal']: s + 'ThumbProximal',
  });
  for (const [f, m] of FINGERS) {
    MAP[s + f + 'Proximal'] = [j + m + '１', j + m + '２'];
    MAP[s + f + 'Intermediate'] = [j + m + '２', j + m + '３'];
    MAP[s + f + 'Distal'] = [j + m + '３', j + m + '３先'];
    CHILD[s + f + 'Proximal'] = s + f + 'Intermediate';
    CHILD[s + f + 'Intermediate'] = s + f + 'Distal';
    INHERIT[s + f + 'Distal'] = s + f + 'Intermediate';
    PARENT[s + f + 'Proximal'] = s + 'Hand';
    PARENT[s + f + 'Intermediate'] = s + f + 'Proximal';
    PARENT[s + f + 'Distal'] = s + f + 'Intermediate';
  }
}

/** Local rotations for the MMD bones at a time, and the hips offset. */
export interface ClipPose {
  rotations: Map<string, THREE.Quaternion>;
  offset: THREE.Vector3;
}

export interface Clip {
  duration: number;
  sample(t: number): ClipPose;
}

interface VrmAnimationExt {
  humanoid: { humanBones: Record<string, { node: number }> };
}

const _q = new THREE.Quaternion();
const _c = new THREE.Quaternion();

/**
 * Loads a VRM Animation and fits it to the model. bindAt(name) gives an MMD
 * bone's bind position in model space.
 */
export async function loadClip(
  url: string,
  bindAt: (bone: string) => THREE.Vector3,
): Promise<Clip> {
  const gltf = await new GLTFLoader().loadAsync(url);
  const ext = gltf.parser.json.extensions?.VRMC_vrm_animation as VrmAnimationExt | undefined;
  if (!ext) throw new Error(`${url} is not a VRM animation`);
  const nodes: Record<string, THREE.Object3D> = {};
  for (const [bone, { node }] of Object.entries(ext.humanoid.humanBones))
    nodes[bone] = (await gltf.parser.getDependency('node', node)) as THREE.Object3D;
  gltf.scene.updateMatrixWorld(true);
  const restPos = (b: string) => nodes[b].getWorldPosition(new THREE.Vector3());

  // Rest corrections.
  const C: Record<string, THREE.Quaternion | undefined> = {};
  for (const [b, [m, mc]] of Object.entries(MAP)) {
    if (CHILD[b] && nodes[CHILD[b]] && nodes[b] && mc) {
      const from = restPos(CHILD[b]).sub(restPos(b)).normalize();
      const to = bindAt(mc).sub(bindAt(m)).normalize();
      C[b] = new THREE.Quaternion().setFromUnitVectors(from, to);
    }
  }
  for (const [b, p] of Object.entries(INHERIT)) C[b] = p ? C[p]?.clone() : new THREE.Quaternion();

  // Hips height: scaled by leg length (thigh to ankle) of each.
  const legClip =
    restPos('leftUpperLeg').distanceTo(restPos('leftLowerLeg')) +
    restPos('leftLowerLeg').distanceTo(restPos('leftFoot'));
  const legModel =
    bindAt('左足').distanceTo(bindAt('左ひざ')) + bindAt('左ひざ').distanceTo(bindAt('左足首'));
  const scale = legModel / legClip;
  const hipsRest = restPos('hips');
  const hipsParent = nodes.hips.parent!.matrixWorld.clone();

  // Rest world rotations of each bone and of its parent node.
  const RW: Record<string, THREE.Quaternion> = {};
  const PW: Record<string, THREE.Quaternion> = {};
  for (const [b, o] of Object.entries(nodes)) {
    RW[b] = o.getWorldQuaternion(new THREE.Quaternion()).invert();
    PW[b] = o.parent!.getWorldQuaternion(new THREE.Quaternion());
  }

  // Interpolants per bone.
  const anim = gltf.animations[0];
  const byName = new Map(Object.entries(nodes).map(([b, o]) => [o.name, b]));
  const tracks: Record<string, Record<string, THREE.Interpolant>> = {};
  for (const tr of anim.tracks) {
    const [name, prop] = tr.name.split('.');
    const bone = byName.get(name);
    if (bone) (tracks[bone] ??= {})[prop] = tr.createInterpolant();
  }
  const rot = (b: string, t: number) => {
    const it = tracks[b]?.quaternion;
    if (!it) return _q.identity();
    return _q
      .fromArray(it.evaluate(t) as unknown as number[])
      .premultiply(PW[b])
      .multiply(RW[b]);
  };

  const out: ClipPose = { rotations: new Map(), offset: new THREE.Vector3() };
  return {
    duration: anim.duration,
    sample(t) {
      for (const [b, [m]] of Object.entries(MAP)) {
        const n = rot(b, t);
        const cp = PARENT[b] ? C[PARENT[b]] : undefined;
        let q = out.rotations.get(m);
        if (!q) out.rotations.set(m, (q = new THREE.Quaternion()));
        q.copy(cp ?? _c.identity()).multiply(n);
        const cb = C[b];
        if (cb) q.multiply(_c.copy(cb).invert());
      }
      // The model has no upper chest: it goes into 上半身2.
      if (tracks.upperChest) out.rotations.get('上半身2')!.multiply(rot('upperChest', t));
      const tr = tracks.hips?.position;
      if (tr)
        out.offset
          .fromArray(tr.evaluate(t) as unknown as number[])
          .applyMatrix4(hipsParent)
          .sub(hipsRest)
          .multiplyScalar(scale);
      else out.offset.set(0, 0, 0);
      return out;
    },
  };
}

/*
 * VMD clips. A VMD made for this PMX names its bones and keys their local
 * rotations and offsets, in MMD's frame; MMDLoader turns it into an
 * AnimationClip in three's frame, as it did the model, with a track
 * `.bones[<name>].quaternion` and `.bones[<name>].position` per bone the
 * skeleton has. Nothing is retargeted: the rotations go on as they are.
 */

/** A pose keyed on one frame is held this long (s). */
export const POSE_HOLD = 6;
const CENTER = 'センター';
const TRACK = /^\.bones\[([^\]]+)\]\.(quaternion|position)$/;

/**
 * A clip over the tracks MMDLoader built. bind(name) is a bone's local bind
 * position, undefined for a bone the model does not have: its tracks are
 * skipped. The offset is センター's position against its bind; the other
 * bones keep their bind positions. Past the last key a track holds its
 * value, so a one-key pose stands for the whole duration.
 */
export function clipFromTracks(
  tracks: THREE.KeyframeTrack[],
  bind: (bone: string) => THREE.Vector3 | undefined,
  duration: number,
): Clip {
  const rot = new Map<string, THREE.Interpolant>();
  let pos: THREE.Interpolant | undefined;
  let centre: THREE.Vector3 | undefined;
  for (const tr of tracks) {
    const m = TRACK.exec(tr.name);
    if (!m) continue;
    const [, bone, prop] = m;
    const b = bind(bone);
    if (!b) continue;
    if (prop === 'quaternion') rot.set(bone, tr.createInterpolant());
    else if (bone === CENTER) {
      pos = tr.createInterpolant();
      centre = b.clone();
    }
  }
  const out: ClipPose = { rotations: new Map(), offset: new THREE.Vector3() };
  for (const bone of rot.keys()) out.rotations.set(bone, new THREE.Quaternion());
  return {
    duration,
    sample(t) {
      for (const [bone, it] of rot)
        out.rotations
          .get(bone)!
          .fromArray(it.evaluate(t) as unknown as number[])
          .normalize();
      if (pos && centre) out.offset.fromArray(pos.evaluate(t) as unknown as number[]).sub(centre);
      else out.offset.set(0, 0, 0);
      return out;
    },
  };
}

/**
 * Loads a VMD made for the model. bind(name) as for clipFromTracks; the
 * mesh gives the bone names. MMDLoader keeps the tracks of the bones its
 * object has and adds each bone's position, as it stands, to the VMD's
 * offsets: a bare skeleton of the model's bones at their bind positions
 * stands in for the mesh, which may be mid-pose when the file arrives. A
 * one-frame pose (duration 0) is held for POSE_HOLD s.
 */
export function loadVmdClip(
  url: string,
  bind: (bone: string) => THREE.Vector3 | undefined,
  mesh: THREE.SkinnedMesh,
): Promise<Clip> {
  const bones: THREE.Bone[] = [];
  for (const { name } of mesh.skeleton.bones) {
    const b = bind(name);
    if (!b) continue;
    const bone = new THREE.Bone();
    bone.name = name;
    bone.position.copy(b);
    bones.push(bone);
  }
  const stand = new THREE.SkinnedMesh();
  stand.skeleton = new THREE.Skeleton(bones);
  stand.morphTargetDictionary = {};
  return new Promise((ok, fail) =>
    new MMDLoader().loadAnimation(
      url,
      stand,
      (a) => {
        const anim = a as THREE.AnimationClip;
        ok(clipFromTracks(anim.tracks, bind, anim.duration > 0 ? anim.duration : POSE_HOLD));
      },
      undefined,
      fail,
    ),
  );
}
