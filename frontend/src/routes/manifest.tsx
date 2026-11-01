import { lazy, type ReactElement } from 'react';
import { Navigate } from 'react-router';
import { Shield, ScrollText } from 'lucide-react';

import { Permission } from '@/lib/auth/permissions';

import { LoginRedirect, RootPage } from '@/pages/Root';

// The account, sign-up and billing pages are code-split too: they carry zod
// and react-hook-form, which a signed-in visit to a section never needs.
// Layout wraps their Outlet in a <Suspense> boundary.
const AboutPage = lazy(() => import('@/pages/About').then((m) => ({ default: m.AboutPage })));
const PrivacyPage = lazy(() => import('@/pages/Privacy').then((m) => ({ default: m.PrivacyPage })));
const RegisterPage = lazy(() =>
  import('@/pages/Register').then((m) => ({ default: m.RegisterPage })),
);
const CheckEmailPage = lazy(() =>
  import('@/pages/CheckEmail').then((m) => ({ default: m.CheckEmailPage })),
);
const ConfirmEmailPage = lazy(() =>
  import('@/pages/ConfirmEmail').then((m) => ({ default: m.ConfirmEmailPage })),
);
const ConfirmChangeEmailPage = lazy(() =>
  import('@/pages/ConfirmChangeEmail').then((m) => ({ default: m.ConfirmChangeEmailPage })),
);
const UnconfirmedPage = lazy(() =>
  import('@/pages/Unconfirmed').then((m) => ({ default: m.UnconfirmedPage })),
);
const ProfilePage = lazy(() => import('@/pages/Profile').then((m) => ({ default: m.ProfilePage })));
const ChangePasswordPage = lazy(() =>
  import('@/pages/ChangePassword').then((m) => ({ default: m.ChangePasswordPage })),
);
const ChangeEmailPage = lazy(() =>
  import('@/pages/ChangeEmail').then((m) => ({ default: m.ChangeEmailPage })),
);
const RequestResetPage = lazy(() =>
  import('@/pages/RequestReset').then((m) => ({ default: m.RequestResetPage })),
);
const ResetPasswordPage = lazy(() =>
  import('@/pages/ResetPassword').then((m) => ({ default: m.ResetPasswordPage })),
);
const JoinFromInvitePage = lazy(() =>
  import('@/pages/JoinFromInvite').then((m) => ({ default: m.JoinFromInvitePage })),
);
const BillingPage = lazy(() => import('@/pages/Billing').then((m) => ({ default: m.BillingPage })));
const BillingReturnPage = lazy(() =>
  import('@/pages/BillingReturn').then((m) => ({ default: m.BillingReturnPage })),
);
const BillingCancelPage = lazy(() =>
  import('@/pages/BillingCancel').then((m) => ({ default: m.BillingCancelPage })),
);

// Admin pages are code-split: a logged-out visitor on /login should not pull
// the whole admin bundle. React.lazy needs a module with a `default` export,
// so each factory maps the page's named export. App.tsx wraps these routes in
// a <Suspense> boundary. (named → default shim kept inline to avoid per-page
// barrel files.)
// The Health section pulls in the chart library; keep it out of the sign-in bundle.
const HealthPage = lazy(() => import('@/pages/Health').then((m) => ({ default: m.HealthPage })));
const WorkoutHomePage = lazy(() =>
  import('@/pages/workout/Home').then((m) => ({ default: m.WorkoutHomePage })),
);
const WorkoutSessionPage = lazy(() =>
  import('@/pages/workout/Session').then((m) => ({ default: m.WorkoutSessionPage })),
);
const WorkoutHistoryPage = lazy(() =>
  import('@/pages/workout/History').then((m) => ({ default: m.WorkoutHistoryPage })),
);
const WorkoutHistoryDetailPage = lazy(() =>
  import('@/pages/workout/HistoryDetail').then((m) => ({ default: m.WorkoutHistoryDetailPage })),
);
const WorkoutExercisesPage = lazy(() =>
  import('@/pages/workout/Exercises').then((m) => ({ default: m.WorkoutExercisesPage })),
);
const WorkoutRoutinesPage = lazy(() =>
  import('@/pages/workout/Routines').then((m) => ({ default: m.WorkoutRoutinesPage })),
);
const WorkoutRoutineEditorPage = lazy(() =>
  import('@/pages/workout/RoutineEditor').then((m) => ({ default: m.WorkoutRoutineEditorPage })),
);
const FoodDayPage = lazy(() =>
  import('@/pages/food/Day').then((m) => ({ default: m.FoodDayPage })),
);
const FoodWeekPage = lazy(() =>
  import('@/pages/food/Week').then((m) => ({ default: m.FoodWeekPage })),
);
const FoodItemsPage = lazy(() =>
  import('@/pages/food/Items').then((m) => ({ default: m.FoodItemsPage })),
);
const FoodGoalsPage = lazy(() =>
  import('@/pages/food/Goals').then((m) => ({ default: m.FoodGoalsPage })),
);
const MoneyLedgerPage = lazy(() =>
  import('@/pages/money/Ledger').then((m) => ({ default: m.MoneyLedgerPage })),
);
const MoneySummaryPage = lazy(() =>
  import('@/pages/money/Summary').then((m) => ({ default: m.MoneySummaryPage })),
);
const MoneyAccountsPage = lazy(() =>
  import('@/pages/money/Accounts').then((m) => ({ default: m.MoneyAccountsPage })),
);
const MoneyCategoriesPage = lazy(() =>
  import('@/pages/money/Categories').then((m) => ({ default: m.MoneyCategoriesPage })),
);
const MoneyCurrenciesPage = lazy(() =>
  import('@/pages/money/Currencies').then((m) => ({ default: m.MoneyCurrenciesPage })),
);
const MoneyReportsPage = lazy(() =>
  import('@/pages/money/Reports').then((m) => ({ default: m.MoneyReportsPage })),
);
const MoneyAdvisorPage = lazy(() =>
  import('@/pages/money/Advisor').then((m) => ({ default: m.MoneyAdvisorPage })),
);
const TasksTodayPage = lazy(() =>
  import('@/pages/tasks/Today').then((m) => ({ default: m.TasksTodayPage })),
);
const TasksLaterPage = lazy(() =>
  import('@/pages/tasks/Later').then((m) => ({ default: m.TasksLaterPage })),
);
const TasksInboxPage = lazy(() =>
  import('@/pages/tasks/Inbox').then((m) => ({ default: m.TasksInboxPage })),
);
const TasksProjectsPage = lazy(() =>
  import('@/pages/tasks/Projects').then((m) => ({ default: m.TasksProjectsPage })),
);
const TasksProjectPage = lazy(() =>
  import('@/pages/tasks/Project').then((m) => ({ default: m.TasksProjectPage })),
);
const NowPage = lazy(() => import('@/pages/now/Now').then((m) => ({ default: m.NowPage })));
const GoalsPage = lazy(() => import('@/pages/goals/Goals').then((m) => ({ default: m.GoalsPage })));
const HabitsTodayPage = lazy(() =>
  import('@/pages/habits/Today').then((m) => ({ default: m.HabitsTodayPage })),
);
const HabitsWeekPage = lazy(() =>
  import('@/pages/habits/Week').then((m) => ({ default: m.HabitsWeekPage })),
);
const TravelTripsPage = lazy(() =>
  import('@/pages/travel/Trips').then((m) => ({ default: m.TravelTripsPage })),
);
const TravelDaysPage = lazy(() =>
  import('@/pages/travel/Days').then((m) => ({ default: m.TravelDaysPage })),
);
const LearningPage = lazy(() =>
  import('@/pages/learning/List').then((m) => ({ default: m.LearningPage })),
);
const LearningCoursePage = lazy(() =>
  import('@/pages/learning/Course').then((m) => ({ default: m.LearningCoursePage })),
);
const NotesPage = lazy(() => import('@/pages/notes/List').then((m) => ({ default: m.NotesPage })));
const NoteEditorPage = lazy(() =>
  import('@/pages/notes/Editor').then((m) => ({ default: m.NoteEditorPage })),
);
const PracticeDealsPage = lazy(() =>
  import('@/pages/practice/Deals').then((m) => ({ default: m.PracticeDealsPage })),
);
const PracticeMoneyPage = lazy(() =>
  import('@/pages/practice/Money').then((m) => ({ default: m.PracticeMoneyPage })),
);
const PracticeMonthsPage = lazy(() =>
  import('@/pages/practice/Months').then((m) => ({ default: m.PracticeMonthsPage })),
);
const PracticeMonthPage = lazy(() =>
  import('@/pages/practice/Month').then((m) => ({ default: m.PracticeMonthPage })),
);
const PracticeClientsPage = lazy(() =>
  import('@/pages/practice/Clients').then((m) => ({ default: m.PracticeClientsPage })),
);
const GithubAttentionPage = lazy(() =>
  import('@/pages/github/List').then((m) => ({ default: m.GithubAttentionPage })),
);
const GithubAllPage = lazy(() =>
  import('@/pages/github/List').then((m) => ({ default: m.GithubAllPage })),
);
const GithubArchivedPage = lazy(() =>
  import('@/pages/github/List').then((m) => ({ default: m.GithubArchivedPage })),
);
const TravelRulesPage = lazy(() =>
  import('@/pages/travel/Rules').then((m) => ({ default: m.TravelRulesPage })),
);
const TravelDocumentsPage = lazy(() =>
  import('@/pages/travel/Documents').then((m) => ({ default: m.TravelDocumentsPage })),
);
const MedicalRecordsPage = lazy(() =>
  import('@/pages/medical/Records').then((m) => ({ default: m.MedicalRecordsPage })),
);
const MedicalVisitsPage = lazy(() =>
  import('@/pages/medical/Visits').then((m) => ({ default: m.MedicalVisitsPage })),
);
const MedicalMedicinesPage = lazy(() =>
  import('@/pages/medical/Medicines').then((m) => ({ default: m.MedicalMedicinesPage })),
);
const MedicalFamilyPage = lazy(() =>
  import('@/pages/medical/Family').then((m) => ({ default: m.MedicalFamilyPage })),
);
const TravelTripPage = lazy(() =>
  import('@/pages/travel/Trip').then((m) => ({ default: m.TravelTripPage })),
);
const AdminDashboardPage = lazy(() =>
  import('@/pages/admin/Dashboard').then((m) => ({ default: m.AdminDashboardPage })),
);
const AdminUsersPage = lazy(() =>
  import('@/pages/admin/Users').then((m) => ({ default: m.AdminUsersPage })),
);
const AdminUserDetailPage = lazy(() =>
  import('@/pages/admin/UserDetail').then((m) => ({ default: m.AdminUserDetailPage })),
);
const AdminInviteUserPage = lazy(() =>
  import('@/pages/admin/InviteUser').then((m) => ({ default: m.AdminInviteUserPage })),
);
const AdminRolesPage = lazy(() =>
  import('@/pages/admin/Roles').then((m) => ({ default: m.AdminRolesPage })),
);
const AdminJobsPage = lazy(() =>
  import('@/pages/admin/Jobs').then((m) => ({ default: m.AdminJobsPage })),
);
const AdminAuditPage = lazy(() =>
  import('@/pages/admin/Audit').then((m) => ({ default: m.AdminAuditPage })),
);
const AdminLibraryPage = lazy(() =>
  import('@/pages/admin/Library').then((m) => ({ default: m.AdminLibraryPage })),
);
const AdminBillingPage = lazy(() =>
  import('@/pages/admin/Billing').then((m) => ({ default: m.AdminBillingPage })),
);

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
  { path: '/now', element: <NowPage />, guard: 'confirmed', layout: 'shell' },
  { path: '/health', element: <HealthPage />, guard: 'confirmed', layout: 'shell' },
  // The medical archive: tabs of Body; the medical module switch is the only gate.
  {
    path: '/health/records',
    element: <MedicalRecordsPage />,
    guard: 'confirmed',
    layout: 'shell',
  },
  { path: '/health/visits', element: <MedicalVisitsPage />, guard: 'confirmed', layout: 'shell' },
  {
    path: '/health/medicines',
    element: <MedicalMedicinesPage />,
    guard: 'confirmed',
    layout: 'shell',
  },
  { path: '/health/family', element: <MedicalFamilyPage />, guard: 'confirmed', layout: 'shell' },
  { path: '/workout', element: <WorkoutHomePage />, guard: 'confirmed', layout: 'shell' },
  {
    path: '/workout/session',
    element: <WorkoutSessionPage />,
    guard: 'confirmed',
    layout: 'shell',
  },
  {
    path: '/workout/history',
    element: <WorkoutHistoryPage />,
    guard: 'confirmed',
    layout: 'shell',
  },
  {
    path: '/workout/history/:id',
    element: <WorkoutHistoryDetailPage />,
    guard: 'confirmed',
    layout: 'shell',
  },
  {
    path: '/workout/exercises',
    element: <WorkoutExercisesPage />,
    guard: 'confirmed',
    layout: 'shell',
  },
  {
    path: '/workout/routines',
    element: <WorkoutRoutinesPage />,
    guard: 'confirmed',
    layout: 'shell',
  },
  // `new` is a reserved id: the editor makes a fresh one and creates on save.
  {
    path: '/workout/routines/:id',
    element: <WorkoutRoutineEditorPage />,
    guard: 'confirmed',
    layout: 'shell',
  },

  // ── Food — any confirmed user; the module switch is the only gate ────────
  { path: '/food', element: <FoodDayPage />, guard: 'confirmed', layout: 'shell' },
  { path: '/food/week', element: <FoodWeekPage />, guard: 'confirmed', layout: 'shell' },
  { path: '/food/items', element: <FoodItemsPage />, guard: 'confirmed', layout: 'shell' },
  { path: '/food/goals', element: <FoodGoalsPage />, guard: 'confirmed', layout: 'shell' },

  // ── Money — any confirmed user; the module switch is the only gate ───────
  { path: '/money', element: <MoneyLedgerPage />, guard: 'confirmed', layout: 'shell' },
  { path: '/money/summary', element: <MoneySummaryPage />, guard: 'confirmed', layout: 'shell' },
  { path: '/money/accounts', element: <MoneyAccountsPage />, guard: 'confirmed', layout: 'shell' },
  {
    path: '/money/categories',
    element: <MoneyCategoriesPage />,
    guard: 'confirmed',
    layout: 'shell',
  },
  {
    path: '/money/currencies',
    element: <MoneyCurrenciesPage />,
    guard: 'confirmed',
    layout: 'shell',
  },
  { path: '/money/reports', element: <MoneyReportsPage />, guard: 'confirmed', layout: 'shell' },
  { path: '/money/advisor', element: <MoneyAdvisorPage />, guard: 'confirmed', layout: 'shell' },

  // ── Tasks — any confirmed user; the module switch is the only gate ───────
  { path: '/tasks', element: <TasksTodayPage />, guard: 'confirmed', layout: 'shell' },
  { path: '/tasks/later', element: <TasksLaterPage />, guard: 'confirmed', layout: 'shell' },
  { path: '/tasks/inbox', element: <TasksInboxPage />, guard: 'confirmed', layout: 'shell' },
  {
    path: '/tasks/projects',
    element: <TasksProjectsPage />,
    guard: 'confirmed',
    layout: 'shell',
  },
  {
    path: '/tasks/projects/:id',
    element: <TasksProjectPage />,
    guard: 'confirmed',
    layout: 'shell',
  },

  // ── Goals — any confirmed user; the module switch is the only gate ───────
  { path: '/goals', element: <GoalsPage />, guard: 'confirmed', layout: 'shell' },

  // ── Habits — any confirmed user; the module switch is the only gate ──────
  { path: '/habits', element: <HabitsTodayPage />, guard: 'confirmed', layout: 'shell' },
  { path: '/habits/week', element: <HabitsWeekPage />, guard: 'confirmed', layout: 'shell' },
  // ── Learning — courses are projects of Tasks; the tasks switch gates it ──
  { path: '/learning', element: <LearningPage />, guard: 'confirmed', layout: 'shell' },
  {
    path: '/learning/:id',
    element: <LearningCoursePage />,
    guard: 'confirmed',
    layout: 'shell',
  },

  // ── Notes — any confirmed user; the module switch is the only gate ───────
  { path: '/notes', element: <NotesPage />, guard: 'confirmed', layout: 'shell' },
  { path: '/notes/:id', element: <NoteEditorPage />, guard: 'confirmed', layout: 'shell' },

  // ── Practice — any confirmed user; the module switch is the only gate ────
  { path: '/practice', element: <PracticeDealsPage />, guard: 'confirmed', layout: 'shell' },
  {
    path: '/practice/money',
    element: <PracticeMoneyPage />,
    guard: 'confirmed',
    layout: 'shell',
  },
  {
    path: '/practice/months',
    element: <PracticeMonthsPage />,
    guard: 'confirmed',
    layout: 'shell',
  },
  {
    path: '/practice/months/:month',
    element: <PracticeMonthPage />,
    guard: 'confirmed',
    layout: 'shell',
  },
  {
    path: '/practice/clients',
    element: <PracticeClientsPage />,
    guard: 'confirmed',
    layout: 'shell',
  },

  // ── GitHub — any confirmed user; the module switch is the only gate ──────
  { path: '/github', element: <GithubAttentionPage />, guard: 'confirmed', layout: 'shell' },
  { path: '/github/all', element: <GithubAllPage />, guard: 'confirmed', layout: 'shell' },
  {
    path: '/github/archived',
    element: <GithubArchivedPage />,
    guard: 'confirmed',
    layout: 'shell',
  },

  // ── Travel — any confirmed user; the module switch is the only gate ──────
  { path: '/travel', element: <TravelTripsPage />, guard: 'confirmed', layout: 'shell' },
  { path: '/travel/days', element: <TravelDaysPage />, guard: 'confirmed', layout: 'shell' },
  { path: '/travel/rules', element: <TravelRulesPage />, guard: 'confirmed', layout: 'shell' },
  {
    path: '/travel/documents',
    element: <TravelDocumentsPage />,
    guard: 'confirmed',
    layout: 'shell',
  },
  { path: '/travel/trips/:id', element: <TravelTripPage />, guard: 'confirmed', layout: 'shell' },

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
