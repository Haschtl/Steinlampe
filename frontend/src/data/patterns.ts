export const patternLabels = [
  // Soft / Ambient
  'Konstant',
  'Atmung',
  'Atmung Warm',
  'Atmung 2',
  'Sinus',
  'Zig-Zag',
  'Saegezahn',
  'Pulsierend',
  'Comet',
  'Aurora',
  'Funkeln',
  'Gluehwuermchen',
  'Sonnenuntergang',
  // Pulse / Alert
  'Heartbeat',
  'Heartbeat Alarm',
  'Alert',
  'SOS',
  'Stufen',
  'Zwinkern',
  'Popcorn',
  // Fire / Candle
  'Kerze Soft',
  'Kerze',
  'Lagerfeuer',
  // Sci-Fi / Media
  'TV Static',
  'HAL-9000',
  'Emergency Bridge',
  'Arc Reactor',
  'Warp Core',
  'KITT Scanner',
  'Tron Grid',
  'Saber Idle',
  'Saber Clash',
  // Weather
  'Gewitter',
  'Distant Storm',
  'Rolling Thunder',
  'Heat Lightning',
  'Strobe Front',
  'Sheet Lightning',
  'Mixed Storm',
  // Culture / Signals
  'Polizei DE',
  'Camera',
  'Weihnacht',
  // Utility
  'Custom',
  'Musik',
];

export function patternLabel(idx: number, name?: string) {
  if (name) return name;
  const label = patternLabels[idx - 1];
  return label || `Pattern ${idx}`;
}
