type ClassValue = string | number | null | undefined | false | ClassDictionary | ClassArray;
interface ClassDictionary {
  [id: string]: any;
}
interface ClassArray extends Array<ClassValue> {}

export function cn(...inputs: ClassValue[]): string {
  const result: string[] = [];
  const append = (val: ClassValue) => {
    if (!val) return;
    if (typeof val === 'string' || typeof val === 'number') {
      result.push(String(val));
    } else if (Array.isArray(val)) {
      val.forEach(append);
    } else if (typeof val === 'object') {
      Object.entries(val).forEach(([k, v]) => {
        if (v) result.push(k);
      });
    }
  };
  inputs.forEach(append);
  return result.join(' ');
}
