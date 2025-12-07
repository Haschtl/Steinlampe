import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Trans } from '@/i18n';

type Props = {
  onTouchCalib: () => void;
  onTouchDebug: () => void;
  onTouchDimToggle: (on: boolean) => void;
};

export function TouchCard({ onTouchCalib, onTouchDebug, onTouchDimToggle }: Props) {
  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.touch">Touch</Trans></CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="flex gap-2 flex-wrap">
          <label className="pill cursor-pointer">
            <input type="checkbox" className="accent-accent" onChange={(e) => onTouchDimToggle(e.target.checked)} /> TouchDim
          </label>
          <Button onClick={onTouchCalib}>Calibrate touch</Button>
          <Button onClick={onTouchDebug}>Touch debug</Button>
        </div>
      </CardContent>
    </Card>
  );
}
