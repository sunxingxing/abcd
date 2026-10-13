export class Reader {
  static read() {
    try { return value; }
    catch (error) { return error.constructor.name; }
  }
}
__observe(Reader.read);
let value = __initialize(Reader);
export function probe() { return Reader.read(); }
