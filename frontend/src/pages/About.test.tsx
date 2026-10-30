// @vitest-environment jsdom
import { cleanup, render, screen } from '@testing-library/react';
import { MemoryRouter } from 'react-router';
import { afterEach, describe, expect, it } from 'vitest';

import { AboutPage } from './About';

afterEach(cleanup);

/** The public home page Google's consent screen points to: what the app does, its Google use, the policy. */
describe('AboutPage', () => {
  it('says what Life OS does with Google Calendar and links the privacy policy', () => {
    render(
      <MemoryRouter>
        <AboutPage />
      </MemoryRouter>,
    );
    expect(screen.getByRole('heading', { name: 'Life OS' })).toBeTruthy();
    expect(screen.getByText(/Google Calendar/, { selector: 'h2' })).toBeTruthy();
    expect(screen.getByRole('link', { name: 'Privacy policy' }).getAttribute('href')).toBe(
      '/privacy',
    );
    expect(screen.getByRole('link', { name: 'Sign in' }).getAttribute('href')).toBe('/');
  });
});
