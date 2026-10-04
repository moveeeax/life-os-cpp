import { useState } from 'react';
import { useForm } from 'react-hook-form';
import { zodResolver } from '@hookform/resolvers/zod';
import { Link, useLocation, useNavigate } from 'react-router';
import { Eye, EyeOff, Moon, Sun } from 'lucide-react';
import type { z } from 'zod';

import '@fontsource-variable/outfit';

import { useToast } from '@/components/ui/toaster';
import { useLogin } from '@/hooks/useAuthMutations';
import { useThemeToggle } from '@/hooks/useThemeToggle';
import { apiErrorMessage } from '@/lib/api/client';
import { BRAND } from '@/lib/brand';
import { loginSchema } from '@/lib/schemas/auth';
import { cn } from '@/lib/utils';
import { DASHBOARD_HOME } from '@/shell/sections';

type FormValues = z.infer<typeof loginSchema>;

const labelClass = 'mb-1.5 block text-sm font-medium text-gray-700 dark:text-gray-400';
const inputClass =
  'h-11 w-full appearance-none rounded-lg border bg-transparent px-4 py-2.5 text-sm text-gray-800 shadow-theme-xs placeholder:text-gray-400 focus:ring-3 focus:outline-hidden dark:bg-gray-900 dark:text-white/90 dark:placeholder:text-white/30';
const inputOk =
  'border-gray-300 focus:border-brand-300 focus:ring-brand-500/20 dark:border-gray-700 dark:focus:border-brand-800';
const inputBad =
  'border-error-500 focus:border-error-300 focus:ring-error-500/20 dark:border-error-500 dark:focus:border-error-800';

/**
 * Sign-in page at `/`. Layout and classes after TailAdmin's SignIn (MIT),
 * without social buttons and without a sign-up link; wired to the existing
 * cookie-session login.
 */
export function SignInPage() {
  const navigate = useNavigate();
  const location = useLocation();
  const next = (location.state as { from?: string } | null)?.from ?? DASHBOARD_HOME;

  const toast = useToast();
  const login = useLogin();
  const { dark, toggleTheme } = useThemeToggle();
  const [showPassword, setShowPassword] = useState(false);
  const {
    register,
    handleSubmit,
    formState: { errors, isSubmitting },
  } = useForm<FormValues>({ resolver: zodResolver(loginSchema) });

  const onSubmit = handleSubmit(async (values) => {
    try {
      await login.mutateAsync(values);
      navigate(next, { replace: true });
    } catch (e) {
      toast.error(apiErrorMessage(e));
    }
  });

  const busy = isSubmitting || login.isPending;

  return (
    <div className="relative bg-white p-6 font-outfit sm:p-0 dark:bg-gray-900">
      <div className="relative flex h-screen w-full flex-col justify-center lg:flex-row dark:bg-gray-900">
        <div className="flex flex-1 flex-col">
          <div className="mx-auto flex w-full max-w-md flex-1 flex-col justify-center">
            <div className="mb-5 sm:mb-8">
              <h1 className="mb-2 text-title-sm font-semibold text-gray-800 sm:text-title-md dark:text-white/90">
                Sign in
              </h1>
              <p className="text-sm text-gray-500 dark:text-gray-400">
                Enter your email and password to sign in.
              </p>
            </div>

            <form onSubmit={onSubmit} noValidate>
              <div className="flex flex-col gap-6">
                <div>
                  <label htmlFor="email" className={labelClass}>
                    Email
                  </label>
                  <input
                    id="email"
                    type="email"
                    autoComplete="email"
                    placeholder="you@example.com"
                    aria-invalid={!!errors.email}
                    aria-describedby={errors.email ? 'email-error' : undefined}
                    className={cn(inputClass, errors.email ? inputBad : inputOk)}
                    {...register('email')}
                  />
                  {errors.email && (
                    <p id="email-error" role="alert" className="mt-1.5 text-xs text-error-500">
                      {errors.email.message}
                    </p>
                  )}
                </div>

                <div>
                  <label htmlFor="password" className={labelClass}>
                    Password
                  </label>
                  <div className="relative">
                    <input
                      id="password"
                      type={showPassword ? 'text' : 'password'}
                      autoComplete="current-password"
                      placeholder="Enter your password"
                      aria-invalid={!!errors.password}
                      aria-describedby={errors.password ? 'password-error' : undefined}
                      className={cn(inputClass, 'pe-12', errors.password ? inputBad : inputOk)}
                      {...register('password')}
                    />
                    <button
                      type="button"
                      onClick={() => setShowPassword((v) => !v)}
                      aria-label={showPassword ? 'Hide password' : 'Show password'}
                      className="absolute inset-e-4 top-1/2 z-10 -translate-y-1/2 text-gray-500 dark:text-gray-400"
                    >
                      {showPassword ? (
                        <Eye className="size-5" aria-hidden="true" />
                      ) : (
                        <EyeOff className="size-5" aria-hidden="true" />
                      )}
                    </button>
                  </div>
                  {errors.password && (
                    <p id="password-error" role="alert" className="mt-1.5 text-xs text-error-500">
                      {errors.password.message}
                    </p>
                  )}
                </div>

                <div className="flex items-center justify-end">
                  <Link
                    to="/account/reset-password"
                    className="text-sm text-brand-500 hover:text-brand-600 dark:text-brand-400"
                  >
                    Forgot password?
                  </Link>
                </div>

                <button
                  type="submit"
                  disabled={busy}
                  className="inline-flex w-full items-center justify-center rounded-lg bg-brand-500 px-4 py-3 text-sm font-medium text-white shadow-theme-xs transition hover:bg-brand-600 disabled:cursor-not-allowed disabled:bg-brand-300"
                >
                  {busy ? 'Signing in…' : 'Sign in'}
                </button>
              </div>
            </form>
          </div>
        </div>

        <div className="hidden h-full w-full items-center bg-brand-950 lg:grid lg:w-1/2 dark:bg-white/5">
          <div className="flex flex-col items-center justify-center">
            <span className="mb-4 block text-title-md font-semibold text-white">{BRAND}</span>
            <p className="max-w-xs text-center text-gray-400 dark:text-white/60">
              Personal operating system: health, days, money and plans in one place.
            </p>
          </div>
        </div>

        <div className="fixed inset-e-6 bottom-6 z-50 hidden sm:block">
          <button
            type="button"
            onClick={toggleTheme}
            aria-label={dark ? 'Switch to light theme' : 'Switch to dark theme'}
            className="inline-flex size-14 items-center justify-center rounded-full bg-brand-500 text-white transition-colors hover:bg-brand-600"
          >
            {dark ? (
              <Sun className="size-5" aria-hidden="true" />
            ) : (
              <Moon className="size-5" aria-hidden="true" />
            )}
          </button>
        </div>
      </div>
    </div>
  );
}
