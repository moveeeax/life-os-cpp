// @vitest-environment jsdom
import { cleanup, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';

import { LIMITED_USE, PrivacyPage } from './Privacy';

afterEach(cleanup);

describe('PrivacyPage', () => {
  it('states Google Limited Use word for word and names a contact', () => {
    render(<PrivacyPage />);
    expect(screen.getByText(LIMITED_USE)).toBeTruthy();
    expect(LIMITED_USE).toContain(
      'Google API Services User Data Policy, including the Limited Use requirements',
    );
    expect(screen.getByText('michael.tarassov@gmail.com')).toBeTruthy();
  });
});
