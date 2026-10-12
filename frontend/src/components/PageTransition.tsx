import type { ComponentType } from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import { Outlet, useMatch, useMatches } from '@tanstack/react-router';

// Keying a wrapper div around a live <Outlet/> doesn't work: AnimatePresence keeps the "exiting"
// element mounted to animate it out, but Outlet stays subscribed to router context and re-renders
// with the *new* route's content mid-exit anyway - the page flashes in, then the transition
// restarts. TanStack Router's own Framer Motion example avoids this by keying the <Outlet>
// element itself with the next match's id, so React treats the previous Outlet as a distinct,
// unmounting element instead of a live one: https://tanstack.com/router -> with-framer-motion.
export function AnimatedOutlet() {
  const matches = useMatches();
  const match = useMatch({ strict: false });
  const nextMatchIndex = matches.findIndex((d) => d.id === match.id) + 1;
  const nextMatch = matches[nextMatchIndex];
  return (
    <AnimatePresence mode="wait">
      <Outlet key={nextMatch?.id} />
    </AnimatePresence>
  );
}

const pageTransitionProps = {
  initial: { opacity: 0, y: 18, scale: 0.96, rotate: -0.4 },
  animate: {
    opacity: 1,
    y: 0,
    scale: 1,
    rotate: 0,
    transition: { duration: 0.55, ease: [0.19, 1, 0.22, 1] },
  },
  exit: {
    opacity: 0,
    y: -14,
    scale: 0.985,
    rotate: 0.35,
    transition: { duration: 0.32, ease: [0.4, 0, 0.7, 0.2] },
  },
} as const;

// Each route's own component must be the thing that animates (not a wrapper around its Outlet) -
// that's what AnimatedOutlet's key swap actually unmounts/mounts.
export function withPageTransition<P extends object>(Component: ComponentType<P>) {
  return function PageTransitionWrapper(props: P) {
    return (
      <motion.div {...pageTransitionProps}>
        <Component {...props} />
      </motion.div>
    );
  };
}
