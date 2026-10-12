import { createHashHistory, createRootRoute, createRoute, createRouter } from '@tanstack/react-router';
import { RootLayout } from './App';
import { AnimatedOutlet, withPageTransition } from './components/PageTransition';
import { HomeSection } from './sections/HomeSection';
import { SettingsSection } from './sections/SettingsSection';
import { ActionsSection } from './sections/ActionsSection';
import { FilterSection } from './sections/FilterSection';
import { HelpSection } from './sections/HelpSection';
import { AdvancedOverview } from './sections/advanced/AdvancedOverview';
import { AnwesenheitPage, LichtPage, MusikPage, PotiPage, RadarPage, TasterPage, TouchPage } from './sections/advanced/subpages';

const rootRoute = createRootRoute({ component: RootLayout });

const homeRoute = createRoute({ getParentRoute: () => rootRoute, path: '/', component: withPageTransition(HomeSection) });
const settingsRoute = createRoute({ getParentRoute: () => rootRoute, path: '/settings', component: withPageTransition(SettingsSection) });
const actionsRoute = createRoute({ getParentRoute: () => rootRoute, path: '/actions', component: withPageTransition(ActionsSection) });
const filtersRoute = createRoute({ getParentRoute: () => rootRoute, path: '/filters', component: withPageTransition(FilterSection) });
const helpRoute = createRoute({ getParentRoute: () => rootRoute, path: '/help', component: withPageTransition(HelpSection) });

// AnimatedOutlet, not a plain Outlet: /advanced has its own sub-routes (poti/taster/...) and
// should animate between them the same way the top-level tabs do.
const advancedRoute = createRoute({ getParentRoute: () => rootRoute, path: '/advanced', component: AnimatedOutlet });
const advancedIndexRoute = createRoute({ getParentRoute: () => advancedRoute, path: '/', component: withPageTransition(AdvancedOverview) });
const potiRoute = createRoute({ getParentRoute: () => advancedRoute, path: 'poti', component: withPageTransition(PotiPage) });
const tasterRoute = createRoute({ getParentRoute: () => advancedRoute, path: 'taster', component: withPageTransition(TasterPage) });
const anwesenheitRoute = createRoute({ getParentRoute: () => advancedRoute, path: 'anwesenheit', component: withPageTransition(AnwesenheitPage) });
const musikRoute = createRoute({ getParentRoute: () => advancedRoute, path: 'musik', component: withPageTransition(MusikPage) });
const radarRoute = createRoute({ getParentRoute: () => advancedRoute, path: 'radar', component: withPageTransition(RadarPage) });
const lichtRoute = createRoute({ getParentRoute: () => advancedRoute, path: 'licht', component: withPageTransition(LichtPage) });
const touchRoute = createRoute({ getParentRoute: () => advancedRoute, path: 'touch', component: withPageTransition(TouchPage) });

const routeTree = rootRoute.addChildren([
  homeRoute,
  settingsRoute,
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
