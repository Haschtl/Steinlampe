import { useCallback, useEffect, useRef, useState } from 'react';

type Options = {
  /** How close the device value must come to the edited value to count as "caught up". */
  tolerance?: number;
  /** Give up waiting for the device to converge after this many ms (e.g. when it clamps). */
  timeoutMs?: number;
  /** Optional command sender. When set, `edit` throttles it (leading + trailing). */
  send?: (v: number) => void;
  /** Minimum interval between throttled sends while dragging. */
  throttleMs?: number;
};

/**
 * Two-way synced numeric value for controls bound to a device state.
 *
 * It normally mirrors the incoming device value, but right after a local edit
 * it holds the optimistic value and ignores device echoes until the device
 * converges to it (or `timeoutMs` elapses). This prevents a slider/input from
 * flickering back to stale or mid-ramp values the device reports while it is
 * still catching up to the command we just sent.
 *
 * If `send` is provided, `edit` also forwards the value to the device throttled
 * to `throttleMs` (sends immediately on the first move, then coalesces and
 * always emits the final value on the trailing edge), so dragging a slider
 * doesn't flood the link.
 *
 * @param deviceValue the latest value reported by the device (may be undefined)
 * @returns `[value, edit, release]` — current display value, an edit callback to
 *          call on user input (takes local control + throttled send), and a
 *          manual release of the optimistic hold.
 */
export function useSyncedValue(
  deviceValue: number | undefined,
  opts: Options = {},
): [number | undefined, (v: number) => void, () => void] {
  const { tolerance = 1, timeoutMs = 4000, send, throttleMs = 80 } = opts;
  const [value, setValue] = useState<number | undefined>(deviceValue);
  const pending = useRef<{ target: number; until: number } | null>(null);

  // Always call the latest sender without re-wiring the throttle timer.
  const sendRef = useRef(send);
  sendRef.current = send;

  // Throttle bookkeeping.
  const lastSentMs = useRef(0);
  const trailingValue = useRef<number | null>(null);
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(() => {
    if (deviceValue === undefined) return;
    const p = pending.current;
    if (p) {
      // Hold the optimistic value until the device catches up or we time out.
      if (Math.abs(deviceValue - p.target) <= tolerance || Date.now() > p.until) {
        pending.current = null;
      } else {
        return;
      }
    }
    setValue(deviceValue);
  }, [deviceValue, tolerance]);

  const flush = useCallback(() => {
    timer.current = null;
    if (trailingValue.current !== null && sendRef.current) {
      const v = trailingValue.current;
      trailingValue.current = null;
      lastSentMs.current = Date.now();
      sendRef.current(v);
    }
  }, []);

  const edit = useCallback(
    (v: number) => {
      // Refresh the optimistic hold to the most recent edited value.
      pending.current = { target: v, until: Date.now() + timeoutMs };
      setValue(v);

      if (!sendRef.current) return;
      const now = Date.now();
      const since = now - lastSentMs.current;
      if (since >= throttleMs) {
        lastSentMs.current = now;
        trailingValue.current = null;
        sendRef.current(v);
      } else {
        trailingValue.current = v;
        if (!timer.current) timer.current = setTimeout(flush, throttleMs - since);
      }
    },
    [flush, throttleMs, timeoutMs],
  );

  useEffect(
    () => () => {
      if (timer.current) clearTimeout(timer.current);
    },
    [],
  );

  const release = useCallback(() => {
    pending.current = null;
  }, []);

  return [value, edit, release];
}
