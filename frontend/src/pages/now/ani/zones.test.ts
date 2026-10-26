import { describe, expect, it } from 'vitest';

import { zoneOf, zoneWords } from './zones';

/** A bone and its parents, nearest first. */
const chain = (...names: string[]) =>
  names.map((name, i) => ({ name, parent: names[i + 1] ?? null }));

describe('zoneOf', () => {
  it('tells the face and the hair from the rest of the head by material', () => {
    expect(zoneOf(chain('頭', '首'), 'Face_Skin')).toEqual({ part: 'face', side: null });
    expect(zoneOf(chain('頭', '首'), 'Hair')).toEqual({ part: 'hair', side: null });
    expect(zoneOf(chain('頭', '首'), 'Body_Skin')).toEqual({ part: 'head', side: null });
    expect(zoneOf(chain('HairB5', 'HairB4', '頭'), 'Hair')).toEqual({ part: 'hair', side: null });
  });

  it('reads her side from the bone: 左 is her left', () => {
    expect(zoneOf(chain('左手首', '左手捩'), 'Body_Skin')).toEqual({ part: 'hand', side: 'left' });
    expect(zoneOf(chain('右人指２', '右人指１', '右手首'), 'Body_Skin')).toEqual({
      part: 'hand',
      side: 'right',
    });
    expect(zoneOf(chain('Breast_L', '上半身2'), 'Lingerie')).toEqual({
      part: 'chest',
      side: 'left',
    });
    expect(zoneOf(chain('Buttock_R', '下半身'), 'Body_Skin')).toEqual({
      part: 'seat',
      side: 'right',
    });
  });

  it('keeps an ankle from reading as a leg and a twist bone from reading as an upper arm', () => {
    expect(zoneOf(chain('左足首D', '左ひざD', '左足D'), 'Body_Skin')?.part).toBe('foot');
    expect(zoneOf(chain('右足先EX', '右足首D'), 'Body_Skin')?.part).toBe('foot');
    expect(zoneOf(chain('左ひざD', '左足D'), 'Body_Skin')?.part).toBe('shin');
    expect(zoneOf(chain('左足D', '腰キャンセル左'), 'Body_Skin')?.part).toBe('thigh');
    expect(zoneOf(chain('左手捩', '左ひじ'), 'Body_Skin')?.part).toBe('forearm');
    expect(zoneOf(chain('左腕捩', '左腕'), 'Body_Skin')?.part).toBe('upperArm');
    expect(zoneOf(chain('左肩P', '上半身2'), 'Body_Skin')?.part).toBe('shoulder');
  });

  it('splits the trunk into chest, belly and hips', () => {
    expect(zoneOf(chain('上半身2', '上半身'), 'Body_Skin')?.part).toBe('chest');
    expect(zoneOf(chain('上半身', '腰'), 'Body_Skin')?.part).toBe('belly');
    expect(zoneOf(chain('下半身', '腰'), 'Lingerie')?.part).toBe('hips');
  });

  it('gives bows and ribbons the part they hang on, and the skirts their own', () => {
    expect(zoneOf(chain('bow_leg_L_1', '左足D'), 'P1-BackBow')).toEqual({
      part: 'thigh',
      side: 'left',
    });
    expect(zoneOf(chain('P1-Skirt_3_2', 'P1-Skirt_3_1', '下半身'), 'P1-OutSkirt')).toEqual({
      part: 'skirt',
      side: null,
    });
  });

  it('has no zone for bones outside her body', () => {
    expect(zoneOf(chain('センター', '全ての親'), 'Body_Skin')).toBeNull();
  });
});

describe('zoneWords', () => {
  it('names the zone with her side', () => {
    expect(zoneWords({ part: 'hand', side: 'left' })).toBe('Left hand');
    expect(zoneWords({ part: 'upperArm', side: 'right' })).toBe('Right upper arm');
    expect(zoneWords({ part: 'face', side: null })).toBe('Face');
  });
});
