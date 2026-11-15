import { lazy, type ComponentType, type ReactElement } from 'react';
import { Navigate } from 'react-router';
import { Shield, ScrollText } from 'lucide-react';

import { Permission } from '@/lib/auth/permissions';

import { LoginRedirect, RootPage } from '@/pages/Root';

/**
 * React.lazy over a page's named export: lazy needs a module with a `default`
 * export, so the factory maps the name. The import() stays a literal at each
 * call site, so Vite still splits every page into its own chunk.
 */
function lazyPage<K extends string, M extends Record<K, ComponentType>>(
  load: () => Promise<M>,
  name: K,
) {
  return lazy(() => load().then((m) => ({ default: m[name] })));
}

// The account, sign-up and billing pages are code-split too: they carry zod
// and react-hook-form, which a signed-in visit to a section never needs.
// Layout wraps their Outlet in a <Suspense> boundary.
const AboutPage = lazyPage(() => import('@/pages/About'), 'AboutPage');
const PrivacyPage = lazyPage(() => import('@/pages/Privacy'), 'PrivacyPage');
const RegisterPage = lazyPage(() => import('@/pages/Register'), 'RegisterPage');
const CheckEmailPage = lazyPage(() => import('@/pages/CheckEmail'), 'CheckEmailPage');
const ConfirmEmailPage = lazyPage(() => import('@/pages/ConfirmEmail'), 'ConfirmEmailPage');
const ConfirmChangeEmailPage = lazyPage(
  () => import('@/pages/ConfirmChangeEmail'),
  'ConfirmChangeEmailPage',
);
const UnconfirmedPage = lazyPage(() => import('@/pages/Unconfirmed'), 'UnconfirmedPage');
const ProfilePage = lazyPage(() => import('@/pages/Profile'), 'ProfilePage');
const ChangePasswordPage = lazyPage(() => import('@/pages/ChangePassword'), 'ChangePasswordPage');
const ChangeEmailPage = lazyPage(() => import('@/pages/ChangeEmail'), 'ChangeEmailPage');
const RequestResetPage = lazyPage(() => import('@/pages/RequestReset'), 'RequestResetPage');
const ResetPasswordPage = lazyPage(() => import('@/pages/ResetPassword'), 'ResetPasswordPage');
const JoinFromInvitePage = lazyPage(() => import('@/pages/JoinFromInvite'), 'JoinFromInvitePage');
const BillingPage = lazyPage(() => import('@/pages/Billing'), 'BillingPage');
const BillingReturnPage = lazyPage(() => import('@/pages/BillingReturn'), 'BillingReturnPage');
const BillingCancelPage = lazyPage(() => import('@/pages/BillingCancel'), 'BillingCancelPage');

// Admin pages are code-split: a logged-out visitor on /login should not pull
// the whole admin bundle. lazyPage maps each page's named export to the
// `default` React.lazy needs, without per-page barrel files. App.tsx wraps
// these routes in a <Suspense> boundary.
// The Health section pulls in the chart library; keep it out of the sign-in bundle.
const HealthPage = lazyPage(() => import('@/pages/Health'), 'HealthPage');
const WorkoutHomePage = lazyPage(() => import('@/pages/workout/Home'), 'WorkoutHomePage');
const WorkoutSessionPage = lazyPage(() => import('@/pages/workout/Session'), 'WorkoutSessionPage');
const WorkoutHistoryPage = lazyPage(() => import('@/pages/workout/History'), 'WorkoutHistoryPage');
const WorkoutHistoryDetailPage = lazyPage(
  () => import('@/pages/workout/HistoryDetail'),
  'WorkoutHistoryDetailPage',
);
const WorkoutExercisesPage = lazyPage(
  () => import('@/pages/workout/Exercises'),
  'WorkoutExercisesPage',
);
const WorkoutRoutinesPage = lazyPage(
  () => import('@/pages/workout/Routines'),
  'WorkoutRoutinesPage',
);
const WorkoutRoutineEditorPage = lazyPage(
  () => import('@/pages/workout/RoutineEditor'),
  'WorkoutRoutineEditorPage',
);
const FoodDayPage = lazyPage(() => import('@/pages/food/Day'), 'FoodDayPage');
const FoodWeekPage = lazyPage(() => import('@/pages/food/Week'), 'FoodWeekPage');
const FoodItemsPage = lazyPage(() => import('@/pages/food/Items'), 'FoodItemsPage');
const FoodGoalsPage = lazyPage(() => import('@/pages/food/Goals'), 'FoodGoalsPage');
const MoneyLedgerPage = lazyPage(() => import('@/pages/money/Ledger'), 'MoneyLedgerPage');
const MoneySummaryPage = lazyPage(() => import('@/pages/money/Summary'), 'MoneySummaryPage');
const MoneyAccountsPage = lazyPage(() => import('@/pages/money/Accounts'), 'MoneyAccountsPage');
const MoneyCategoriesPage = lazyPage(
  () => import('@/pages/money/Categories'),
  'MoneyCategoriesPage',
);
const MoneyCurrenciesPage = lazyPage(
  () => import('@/pages/money/Currencies'),
  'MoneyCurrenciesPage',
);
const MoneyReportsPage = lazyPage(() => import('@/pages/money/Reports'), 'MoneyReportsPage');
const MoneyAdvisorPage = lazyPage(() => import('@/pages/money/Advisor'), 'MoneyAdvisorPage');
const TasksTodayPage = lazyPage(() => import('@/pages/tasks/Today'), 'TasksTodayPage');
const TasksLaterPage = lazyPage(() => import('@/pages/tasks/Later'), 'TasksLaterPage');
const TasksInboxPage = lazyPage(() => import('@/pages/tasks/Inbox'), 'TasksInboxPage');
const TasksProjectsPage = lazyPage(() => import('@/pages/tasks/Projects'), 'TasksProjectsPage');
const TasksProjectPage = lazyPage(() => import('@/pages/tasks/Project'), 'TasksProjectPage');
const NowPage = lazyPage(() => import('@/pages/now/Now'), 'NowPage');
const GoalsPage = lazyPage(() => import('@/pages/goals/Goals'), 'GoalsPage');
const HabitsTodayPage = lazyPage(() => import('@/pages/habits/Today'), 'HabitsTodayPage');
const HabitsWeekPage = lazyPage(() => import('@/pages/habits/Week'), 'HabitsWeekPage');
const TravelTripsPage = lazyPage(() => import('@/pages/travel/Trips'), 'TravelTripsPage');
const TravelDaysPage = lazyPage(() => import('@/pages/travel/Days'), 'TravelDaysPage');
const LearningPage = lazyPage(() => import('@/pages/learning/List'), 'LearningPage');
const LearningCoursePage = lazyPage(() => import('@/pages/learning/Course'), 'LearningCoursePage');
const NotesPage = lazyPage(() => import('@/pages/notes/List'), 'NotesPage');
const NoteEditorPage = lazyPage(() => import('@/pages/notes/Editor'), 'NoteEditorPage');
const PracticeDealsPage = lazyPage(() => import('@/pages/practice/Deals'), 'PracticeDealsPage');
const PracticeMoneyPage = lazyPage(() => import('@/pages/practice/Money'), 'PracticeMoneyPage');
const PracticeMonthsPage = lazyPage(() => import('@/pages/practice/Months'), 'PracticeMonthsPage');
const PracticeMonthPage = lazyPage(() => import('@/pages/practice/Month'), 'PracticeMonthPage');
const PracticeClientsPage = lazyPage(
  () => import('@/pages/practice/Clients'),
  'PracticeClientsPage',
);
const GithubAttentionPage = lazyPage(() => import('@/pages/github/List'), 'GithubAttentionPage');
const GithubAllPage = lazyPage(() => import('@/pages/github/List'), 'GithubAllPage');
const GithubArchivedPage = lazyPage(() => import('@/pages/github/List'), 'GithubArchivedPage');
const TravelRulesPage = lazyPage(() => import('@/pages/travel/Rules'), 'TravelRulesPage');
const TravelDocumentsPage = lazyPage(
  () => import('@/pages/travel/Documents'),
  'TravelDocumentsPage',
);
const MedicalRecordsPage = lazyPage(() => import('@/pages/medical/Records'), 'MedicalRecordsPage');
const MedicalVisitsPage = lazyPage(() => import('@/pages/medical/Visits'), 'MedicalVisitsPage');
const MedicalMedicinesPage = lazyPage(
  () => import('@/pages/medical/Medicines'),
  'MedicalMedicinesPage',
);
const MedicalFamilyPage = lazyPage(() => import('@/pages/medical/Family'), 'MedicalFamilyPage');
const TravelTripPage = lazyPage(() => import('@/pages/travel/Trip'), 'TravelTripPage');
const AdminDashboardPage = lazyPage(() => import('@/pages/admin/Dashboard'), 'AdminDashboardPage');
const AdminUsersPage = lazyPage(() => import('@/pages/admin/Users'), 'AdminUsersPage');
const AdminUserDetailPage = lazyPage(
  () => import('@/pages/admin/UserDetail'),
  'AdminUserDetailPage',
);
const AdminInviteUserPage = lazyPage(
  () => import('@/pages/admin/InviteUser'),
  'AdminInviteUserPage',
);
const AdminRolesPage = lazyPage(() => import('@/pages/admin/Roles'), 'AdminRolesPage');
const AdminJobsPage = lazyPage(() => import('@/pages/admin/Jobs'), 'AdminJobsPage');
const AdminAuditPage = lazyPage(() => import('@/pages/admin/Audit'), 'AdminAuditPage');
const AdminLibraryPage = lazyPage(() => import('@/pages/admin/Library'), 'AdminLibraryPage');
const AdminBillingPage = lazyPage(() => import('@/pages/admin/Billing'), 'AdminBillingPage');

/**
 * Single routes manifest — THE source of truth for both:
 *   - App.tsx, which renders <Route>s grouped by `guard`, and
 *   - Nav.tsx, which renders the (permission-filtered) nav links.
 *
 * Keeping route ↔ nav in one array kills the drift where a page was
 * mounted but never linked (or linked but mounted under the wrong guard).
 * Add a page by appending one entry here; `scripts/new-react-page.sh`
 * automates the append.
 *
 * Guard semantics (mirror App.tsx's old layout-route groups 1:1):
 *   - 'public'    — no auth required.
 *   - 'auth'      — any signed-in user (confirmed or not). Used by
 *                   /unconfirmed so an unconfirmed user can still land.
 *   - 'confirmed' — signed in AND email-confirmed.
 *   - 'admin'     — signed in, confirmed, AND Permission.Administer
 *                   (the 0x40000000 sentinel bit, not 0xff).
 *
 * `navLabel` opts a route into the top nav. `navIcon` (a lucide icon
 * component) renders before the label. Routes with a dynamic `:param`
 * segment never belong in the nav, so they simply omit `navLabel`.
 *
 * `requirePermission` lets a non-admin route still gate on a specific
 * bit; admin routes imply Permission.Administer via their guard, so they
 * don't repeat it.
 */
export type RouteGuard = 'public' | 'auth' | 'confirmed' | 'admin';
export type RouteLayout = 'classic' | 'shell' | 'bare';

export interface RouteEntry {
  path: string;
  element: ReactElement;
  guard: RouteGuard;
  /** When present, the route shows up in the top nav with this label. */
  navLabel?: string;
  /** Optional lucide icon component rendered before the nav label. */
  navIcon?: React.ComponentType<{ className?: string }>;
  /** Extra permission bit a non-admin route must carry (rare). */
  requirePermission?: number;
  /**
   * Which frame the page renders in. 'classic' (default): the top-nav Layout
   * of the admin and account pages. 'shell': the glass shell with the pill
   * nav. 'bare': no frame (the full-screen sign-in page).
   */
  layout?: RouteLayout;
}

/** A dashboard section: signed in and confirmed, in the glass shell. */
const shell = (path: string, element: ReactElement): RouteEntry => ({
  path,
  element,
  guard: 'confirmed',
  layout: 'shell',
});

export const routes: RouteEntry[] = [
  // ── Public ────────────────────────────────────────────────────────────
  // `/` is the sign-in form for a guest and a redirect to the dashboard for
  // a signed-in user. It keeps the 'Home' nav label so the classic top nav
  // still has a way back to the dashboard.
  { path: '/', element: <RootPage />, guard: 'public', navLabel: 'Home', layout: 'bare' },
  { path: '/about', element: <AboutPage />, guard: 'public', navLabel: 'About' },
  { path: '/privacy', element: <PrivacyPage />, guard: 'public' },
  { path: '/login', element: <LoginRedirect />, guard: 'public', layout: 'bare' },
  { path: '/register', element: <RegisterPage />, guard: 'public' },
  { path: '/account/check-email', element: <CheckEmailPage />, guard: 'public' },
  { path: '/account/confirm/:token', element: <ConfirmEmailPage />, guard: 'public' },
  // Public on purpose: the link lands in the NEW mailbox, where the user
  // may not have a session. The token itself authenticates. v6 ranks the
  // static /account/change-email (confirmed, below) above this :token
  // segment, so they don't conflict.
  {
    path: '/account/change-email/:token',
    element: <ConfirmChangeEmailPage />,
    guard: 'public',
  },
  { path: '/account/reset-password', element: <RequestResetPage />, guard: 'public' },
  { path: '/account/reset-password/:token', element: <ResetPasswordPage />, guard: 'public' },
  {
    path: '/account/join-from-invite/:token',
    element: <JoinFromInvitePage />,
    guard: 'public',
  },

  // ── Authenticated but possibly unconfirmed ──────────────────────────────
  { path: '/unconfirmed', element: <UnconfirmedPage />, guard: 'auth' },

  // ── Authenticated + confirmed ───────────────────────────────────────────
  { path: '/account', element: <ProfilePage />, guard: 'confirmed' },
  { path: '/account/change-password', element: <ChangePasswordPage />, guard: 'confirmed' },
  { path: '/account/change-email', element: <ChangeEmailPage />, guard: 'confirmed' },
  // Billing (wallet / PayPal top-up). No navLabel — the wallet-balance
  // indicator in Nav's account cluster is the entry point (it only renders
  // when GET /billing/wallet succeeds, i.e. when the billing module is
  // enabled on the backend, so a disabled module leaves zero dangling UI).
  // /billing/return and /billing/cancel are PayPal's redirect targets
  // (billing.paypal.return_url / cancel_url, docs/CONFIG.md).
  { path: '/billing', element: <BillingPage />, guard: 'confirmed' },
  { path: '/billing/return', element: <BillingReturnPage />, guard: 'confirmed' },
  { path: '/billing/cancel', element: <BillingCancelPage />, guard: 'confirmed' },

  // ── Dashboard shell ─────────────────────────────────────────────────────
  // Sections of the dashboard. A section checks its own permission and shows
  // a "no access" card: the guards' permission fallback is `/`, which would
  // bounce a signed-in user straight back here.
  // Now: the landing screen of the glass shell (DASHBOARD_HOME).
  shell('/now', <NowPage />),
  shell('/health', <HealthPage />),
  // The medical archive: tabs of Body; the medical module switch is the only gate.
  shell('/health/records', <MedicalRecordsPage />),
  shell('/health/visits', <MedicalVisitsPage />),
  shell('/health/medicines', <MedicalMedicinesPage />),
  shell('/health/family', <MedicalFamilyPage />),
  shell('/workout', <WorkoutHomePage />),
  shell('/workout/session', <WorkoutSessionPage />),
  shell('/workout/history', <WorkoutHistoryPage />),
  shell('/workout/history/:id', <WorkoutHistoryDetailPage />),
  shell('/workout/exercises', <WorkoutExercisesPage />),
  shell('/workout/routines', <WorkoutRoutinesPage />),
  // `new` is a reserved id: the editor makes a fresh one and creates on save.
  shell('/workout/routines/:id', <WorkoutRoutineEditorPage />),

  // ── Food — any confirmed user; the module switch is the only gate ────────
  shell('/food', <FoodDayPage />),
  shell('/food/week', <FoodWeekPage />),
  shell('/food/items', <FoodItemsPage />),
  shell('/food/goals', <FoodGoalsPage />),

  // ── Money — any confirmed user; the module switch is the only gate ───────
  shell('/money', <MoneyLedgerPage />),
  shell('/money/summary', <MoneySummaryPage />),
  shell('/money/accounts', <MoneyAccountsPage />),
  shell('/money/categories', <MoneyCategoriesPage />),
  shell('/money/currencies', <MoneyCurrenciesPage />),
  shell('/money/reports', <MoneyReportsPage />),
  shell('/money/advisor', <MoneyAdvisorPage />),

  // ── Tasks — any confirmed user; the module switch is the only gate ───────
  shell('/tasks', <TasksTodayPage />),
  shell('/tasks/later', <TasksLaterPage />),
  shell('/tasks/inbox', <TasksInboxPage />),
  shell('/tasks/projects', <TasksProjectsPage />),
  shell('/tasks/projects/:id', <TasksProjectPage />),

  // ── Goals — any confirmed user; the module switch is the only gate ───────
  shell('/goals', <GoalsPage />),

  // ── Habits — any confirmed user; the module switch is the only gate ──────
  shell('/habits', <HabitsTodayPage />),
  shell('/habits/week', <HabitsWeekPage />),
  // ── Learning — courses are projects of Tasks; the tasks switch gates it ──
  shell('/learning', <LearningPage />),
  shell('/learning/:id', <LearningCoursePage />),

  // ── Notes — any confirmed user; the module switch is the only gate ───────
  shell('/notes', <NotesPage />),
  shell('/notes/:id', <NoteEditorPage />),

  // ── Practice — any confirmed user; the module switch is the only gate ────
  shell('/practice', <PracticeDealsPage />),
  shell('/practice/money', <PracticeMoneyPage />),
  shell('/practice/months', <PracticeMonthsPage />),
  shell('/practice/months/:month', <PracticeMonthPage />),
  shell('/practice/clients', <PracticeClientsPage />),

  // ── GitHub — any confirmed user; the module switch is the only gate ──────
  shell('/github', <GithubAttentionPage />),
  shell('/github/all', <GithubAllPage />),
  shell('/github/archived', <GithubArchivedPage />),

  // ── Travel — any confirmed user; the module switch is the only gate ──────
  shell('/travel', <TravelTripsPage />),
  shell('/travel/days', <TravelDaysPage />),
  shell('/travel/rules', <TravelRulesPage />),
  shell('/travel/documents', <TravelDocumentsPage />),
  shell('/travel/trips/:id', <TravelTripPage />),

  // ── Admin — gated by Permission.Administer (0x40000000 sentinel) ────────
  {
    path: '/admin',
    element: <AdminDashboardPage />,
    guard: 'admin',
    navLabel: 'Admin',
    navIcon: Shield,
  },
  { path: '/admin/users', element: <AdminUsersPage />, guard: 'admin' },
  { path: '/admin/users/:id', element: <AdminUserDetailPage />, guard: 'admin' },
  { path: '/admin/invite', element: <AdminInviteUserPage />, guard: 'admin' },
  { path: '/admin/roles', element: <AdminRolesPage />, guard: 'admin' },
  { path: '/admin/jobs', element: <AdminJobsPage />, guard: 'admin' },
  { path: '/admin/library', element: <AdminLibraryPage />, guard: 'admin' },
  // The library had only wallpapers before; old links land on it.
  { path: '/admin/wallpapers', element: <Navigate to="/admin/library" replace />, guard: 'admin' },

  // ── Audit — read-only, gated on kAuditRead (0x02) rather than full ──────
  // Administer. A 'confirmed' guard + requirePermission means App.tsx wraps
  // it in a ProtectedRoute for the AuditRead bit and Nav filters the link by
  // it; full admins (the Administer sentinel) satisfy every check, so they
  // see it too.
  {
    path: '/admin/audit',
    element: <AdminAuditPage />,
    guard: 'confirmed',
    requirePermission: Permission.AuditRead,
    navLabel: 'Audit',
    navIcon: ScrollText,
  },
  // No navLabel — tiles-only (the /admin dashboard tile is
  // the entry point, not the top nav).
  { path: '/admin/billing', element: <AdminBillingPage />, guard: 'admin' },
];

/** The permission a guard implies, for nav-link filtering in Nav.tsx. */
export function guardPermission(entry: RouteEntry): number {
  if (entry.guard === 'admin') return Permission.Administer;
  return entry.requirePermission ?? Permission.None;
}
