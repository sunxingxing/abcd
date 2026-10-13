export class Reader {
  static read() { return value; }
  static forward() { return () => value; }
}
__observe(Reader.read);
let value = __initialize(Reader);
export function probe() { return Reader.read() + Reader.forward()(); }
