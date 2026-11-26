import { createHashHistory, createRootRoute, createRoute, createRouter, lazyRouteComponent } from '@tanstack/react-router';
import { RootLayout } from './App';
import { AnimatedOutlet, withPageTransition } from './components/PageTransition';

const rootRoute = createRootRoute({ component: RootLayout });

// Every route's page component is lazy-loaded (lazyRouteComponent) instead of
// a static top-level import, so the entry bundle only pays for code actually
// needed on first paint - the Advanced sub-pages, Settings, Backup, etc. each
// become their own chunk, fetched only when that route is visited. App.tsx
// wraps <AnimatedOutlet/> in the <Suspense> this needs. See memory:
// frontend-code-splitting.
const homeRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: '/',
  component: withPageTransition(lazyRouteComponent(() => import('./sections/HomeSection'), 'HomeSection')),
});
// AnimatedOutlet, not a plain Outlet: /settings now has its own sub-routes (the
// backup import/export subpages) and should animate between them the same way
// the top-level tabs do - same reasoning as /advanced below.
const settingsRoute = createRoute({ getParentRoute: () => rootRoute, path: '/settings', component: AnimatedOutlet });
const settingsIndexRoute = createRoute({
  getParentRoute: () => settingsRoute,
  path: '/',
  component: withPageTransition(lazyRouteComponent(() => import('./sections/SettingsSection'), 'SettingsSection')),
});
const backupImportRoute = createRoute({
  getParentRoute: () => settingsRoute,
  path: 'backup/import',
  component: withPageTransition(lazyRouteComponent(() => import('./sections/settings/BackupPages'), 'BackupImportPage')),
});
const backupExportRoute = createRoute({
  getParentRoute: () => settingsRoute,
  path: 'backup/export',
  component: withPageTransition(lazyRouteComponent(() => import('./sections/settings/BackupPages'), 'BackupExportPage')),
});
const actionsRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: '/actions',
  component: withPageTransition(lazyRouteComponent(() => import('./sections/ActionsSection'), 'ActionsSection')),
});
const filtersRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: '/filters',
  component: withPageTransition(lazyRouteComponent(() => import('./sections/FilterSection'), 'FilterSection')),
});
const helpRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: '/help',
  component: withPageTransition(lazyRouteComponent(() => import('./sections/HelpSection'), 'HelpSection')),
});

// AnimatedOutlet, not a plain Outlet: /advanced has its own sub-routes (poti/taster/...) and
// should animate between them the same way the top-level tabs do.
const advancedRoute = createRoute({ getParentRoute: () => rootRoute, path: '/advanced', component: AnimatedOutlet });
const advancedIndexRoute = createRoute({
  getParentRoute: () => advancedRoute,
  path: '/',
  component: withPageTransition(lazyRouteComponent(() => import('./sections/advanced/AdvancedOverview'), 'AdvancedOverview')),
});
const potiRoute = createRoute({
  getParentRoute: () => advancedRoute,
  path: 'poti',
  component: withPageTransition(lazyRouteComponent(() => import('./sections/advanced/subpages'), 'PotiPage')),
});
const tasterRoute = createRoute({
  getParentRoute: () => advancedRoute,
  path: 'taster',
  component: withPageTransition(lazyRouteComponent(() => import('./sections/advanced/subpages'), 'TasterPage')),
});
const anwesenheitRoute = createRoute({
  getParentRoute: () => advancedRoute,
  path: 'anwesenheit',
  component: withPageTransition(lazyRouteComponent(() => import('./sections/advanced/subpages'), 'AnwesenheitPage')),
});
const musikRoute = createRoute({
  getParentRoute: () => advancedRoute,
  path: 'musik',
  component: withPageTransition(lazyRouteComponent(() => import('./sections/advanced/subpages'), 'MusikPage')),
});
const radarRoute = createRoute({
  getParentRoute: () => advancedRoute,
  path: 'radar',
  component: withPageTransition(lazyRouteComponent(() => import('./sections/advanced/subpages'), 'RadarPage')),
});
const lichtRoute = createRoute({
  getParentRoute: () => advancedRoute,
  path: 'licht',
  component: withPageTransition(lazyRouteComponent(() => import('./sections/advanced/subpages'), 'LichtPage')),
});
const touchRoute = createRoute({
  getParentRoute: () => advancedRoute,
  path: 'touch',
  component: withPageTransition(lazyRouteComponent(() => import('./sections/advanced/subpages'), 'TouchPage')),
});
const ledRoute = createRoute({
  getParentRoute: () => advancedRoute,
  path: 'led',
  component: withPageTransition(lazyRouteComponent(() => import('./sections/advanced/subpages'), 'LedPage')),
});

const routeTree = rootRoute.addChildren([
  homeRoute,
  settingsRoute.addChildren([settingsIndexRoute, backupImportRoute, backupExportRoute]),
  actionsRoute,
  filtersRoute,
  helpRoute,
  advancedRoute.addChildren([
    advancedIndexRoute,
    potiRoute,
    tasterRoute,
    anwesenheitRoute,
    musikRoute,
    radarRoute,
    lichtRoute,
    touchRoute,
    ledRoute,
  ]),
]);

// Hash-based routing: this app deploys as a static bundle (GitHub Pages) with no server-side
// SPA fallback, so a browser-history router would 404 on a direct/refreshed deep link into a
// sub-route. The hash always resolves to the same served index.html regardless of server config.
export const router = createRouter({ routeTree, history: createHashHistory() });

declare module '@tanstack/react-router' {
  interface Register {
    router: typeof router;
  }
}
