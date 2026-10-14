export function factory() {
  return class {
    constructor(value) {
      this.value = value;
      if (value < 0) throw new RangeError('negative');
    }
    read() { return $value0(this.value); }
  };
}
export class Holder {
  other() { return class { constructor(value) { this.value = value; } }; }
  factory() { return class { constructor(value) { this.value = value; } read() { return this.value; } }; }
}
export function repeated(n) {
  let result = [];
  for (let i = 0; i < n; ++i) {
    result[i] = class { constructor(value) { this.value = value; } };
  }
  return result;
}
export function variant(flag) {
  const Original = class { constructor(value) { this.value = value; } };
  const Copy = Original;
  if (flag) { const Again = Copy; return Again; }
  return Original;
}
