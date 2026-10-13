import { describe, expect, it } from 'vitest';

import { pickBody, quickBody } from './bodies';

describe('quickBody', () => {
  it('posts what the quick form posted', () => {
    expect(
      quickBody({
        date: '2026-10-13',
        meal: 'lunch',
        name: ' Plov ',
        kcal: 620,
        protein: '38',
        fat: '',
        carbs: '68,5',
      }),
    ).toEqual({
      date: '2026-10-13',
      meal: 'lunch',
      name: 'Plov',
      kcal: 620,
      protein_g: 38,
      fat_g: 0,
      carbs_g: 68.5,
    });
  });
});

describe('pickBody', () => {
  it('logs an own product by grams', () => {
    expect(pickBody({ date: '2026-10-13', meal: 'breakfast', itemId: 'i1', grams: 250 })).toEqual({
      date: '2026-10-13',
      meal: 'breakfast',
      item_id: 'i1',
      grams: 250,
    });
  });
});
