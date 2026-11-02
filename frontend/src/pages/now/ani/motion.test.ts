import * as THREE from 'three';
import { describe, expect, it } from 'vitest';

import { clipFromTracks, POSE_HOLD } from './motion';

// The model's bones for the test, at their local bind positions.
const BIND: Record<string, THREE.Vector3> = {
  センター: new THREE.Vector3(0, 8, 0),
  上半身: new THREE.Vector3(0, 3, 0),
  頭: new THREE.Vector3(0, 1, 0),
};
const bind = (n: string) => BIND[n]?.clone();

const turn = (deg: number) =>
  new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 0, 1), (deg * Math.PI) / 180);
const rotTrack = (bone: string, times: number[], qs: THREE.Quaternion[]) =>
  new THREE.QuaternionKeyframeTrack(
    `.bones[${bone}].quaternion`,
    times,
    qs.flatMap((q) => q.toArray()),
  );
const posTrack = (bone: string, times: number[], ps: THREE.Vector3[]) =>
  new THREE.VectorKeyframeTrack(
    `.bones[${bone}].position`,
    times,
    ps.flatMap((p) => p.toArray()),
  );

describe('clipFromTracks', () => {
  it('holds a one-frame pose for the whole duration', () => {
    const q = turn(30);
    const clip = clipFromTracks(
      [rotTrack('上半身', [0], [q]), posTrack('センター', [0], [new THREE.Vector3(0.5, 8, 0)])],
      bind,
      POSE_HOLD,
    );
    expect(clip.duration).toBe(6);
    for (const t of [0, 1, 3, 5.9, 6]) {
      const p = clip.sample(t);
      expect(p.rotations.get('上半身')!.angleTo(q)).toBeLessThan(1e-6);
      expect(p.offset.x).toBeCloseTo(0.5);
    }
  });

  it('gives センター as an offset from its bind position', () => {
    const clip = clipFromTracks(
      [posTrack('センター', [0, 1], [new THREE.Vector3(0, 8, 0), new THREE.Vector3(1, 6, -2)])],
      bind,
      1,
    );
    expect(clip.sample(0).offset.toArray()).toEqual([0, 0, 0]);
    expect(clip.sample(1).offset.toArray()).toEqual([1, -2, -2]);
    // Linear between the keys.
    expect(clip.sample(0.5).offset.toArray()).toEqual([0.5, -1, -1]);
  });

  it('turns between keys and rests at identity with no offset track', () => {
    const clip = clipFromTracks([rotTrack('頭', [0, 2], [turn(0), turn(90)])], bind, 2);
    const p = clip.sample(1);
    expect(p.rotations.get('頭')!.angleTo(turn(45))).toBeLessThan(1e-6);
    expect(p.offset.length()).toBe(0);
  });

  it('skips bones the model does not have and tracks it does not read', () => {
    const clip = clipFromTracks(
      [
        rotTrack('左足ＩＫ', [0], [turn(10)]),
        posTrack('頭', [0], [new THREE.Vector3(9, 9, 9)]),
        rotTrack('頭', [0], [turn(10)]),
        new THREE.NumberKeyframeTrack('.morphTargetInfluences[あ]', [0], [1]),
      ],
      bind,
      POSE_HOLD,
    );
    const p = clip.sample(0);
    expect([...p.rotations.keys()]).toEqual(['頭']);
    expect(p.offset.toArray()).toEqual([0, 0, 0]);
  });
});
