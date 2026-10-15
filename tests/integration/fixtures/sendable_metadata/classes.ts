export class Closed {
  value: number;
  constructor(value) {
    "use sendable";
    this.value = 0;
    this.value = value;
  }
  read() { return this.value; }
}
export class Types<T> {
  a: number;
  b: string;
  c: boolean;
  d: bigint;
  e: null;
  f: undefined;
  g: number | string;
  h: T;
  static s: string;
  constructor() { "use sendable"; }
}
