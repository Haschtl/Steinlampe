import { useEffect, useMemo, useState } from 'react';
import { MorseCard } from '@/cards/MorseCard';
import { NotifyCard } from '@/cards/NotifyCard';
import { WakeSleepCard } from '@/cards/WakeSleepCard';
import { useConnection } from '@/context/connection';
import { patternLabel, patternLabels } from '@/data/patterns';

export function ActionsSection() {
  const { status, sendCmd } = useConnection();
  const [notifySeq, setNotifySeq] = useState('80 40 80 120');
  const [notifyFade, setNotifyFade] = useState(0);
  const [notifyRepeat, setNotifyRepeat] = useState(1);
  const [notifyMin, setNotifyMin] = useState(10);
  const [wakeDuration, setWakeDuration] = useState(180);
  const [wakeMode, setWakeMode] = useState('');
  const [wakeBri, setWakeBri] = useState('');
  const [wakeSoft, setWakeSoft] = useState(false);
  const [sleepMinutes, setSleepMinutes] = useState(15);

  const patternOptions = useMemo(() => {
    const count = status.patternCount || patternLabels.length;
    return Array.from({ length: count }, (_, i) => ({
      idx: i + 1,
      label: `${i + 1} - ${patternLabel(i + 1)}`,
    }));
  }, [status.patternCount]);

  const buildNotifyCmd = (seq: string, fade?: number, repeat = 1) => {
    const parts = seq
      .split(/\s+/)
      .map((n) => parseInt(n, 10))
      .filter((n) => !Number.isNaN(n) && n > 0);
    if (!parts.length) return '';
    const expanded: number[] = [];
    for (let i = 0; i < repeat; i += 1) expanded.push(...parts);
    let cmd = `notify ${expanded.join(' ')}`;
    if (fade && fade > 0) cmd += ` fade=${fade}`;
    return cmd;
  };

  const handleNotify = (seq: string, fade?: number, repeat = 1) => {
    const cmd = buildNotifyCmd(seq, fade, repeat);
    if (!cmd) return;
    sendCmd(cmd).catch((e) => console.warn(e));
  };

  // Sync min notify brightness from status if available
  useEffect(() => {
    if (typeof status.notifyMin === 'number' && !Number.isNaN(status.notifyMin)) {
      setNotifyMin(status.notifyMin);
    }
  }, [status.notifyMin]);

  const handleWake = () => {
    const parts = ['wake'];
    if (wakeSoft) parts.push('soft');
    if (wakeMode.trim()) parts.push(`mode=${wakeMode.trim()}`);
    if (wakeBri.trim()) parts.push(`bri=${wakeBri.trim()}`);
    parts.push(String(Math.max(1, wakeDuration || 1)));
    sendCmd(parts.join(' ')).catch((e) => console.warn(e));
  };

  return (
    <div className="space-y-4">
      <WakeSleepCard
        wakeDuration={wakeDuration}
        setWakeDuration={setWakeDuration}
        wakeMode={wakeMode}
        setWakeMode={setWakeMode}
        wakeBri={wakeBri}
        setWakeBri={setWakeBri}
        wakeSoft={wakeSoft}
        setWakeSoft={setWakeSoft}
        handleWake={handleWake}
        sleepMinutes={sleepMinutes}
        setSleepMinutes={setSleepMinutes}
        patternOptions={patternOptions}
        sendCmd={sendCmd}
      />
      <NotifyCard
        notifySeq={notifySeq}
        setNotifySeq={setNotifySeq}
        notifyFade={notifyFade}
        setNotifyFade={setNotifyFade}
        notifyRepeat={notifyRepeat}
        setNotifyRepeat={setNotifyRepeat}
        notifyMin={notifyMin}
        setNotifyMin={setNotifyMin}
        handleNotify={handleNotify}
        sendCmd={sendCmd}
      />
      <MorseCard sendCmd={sendCmd} />
    </div>
  );
}
