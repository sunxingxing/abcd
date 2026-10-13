export class Early {
  static saved = __observe(() => value);
  static read() { return value; }
}
let value = 4;
export function probe() { return Early.read(); }
