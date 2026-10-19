export class C {
  constructor() {
    globalThis.log.push("constructor");
    throw new RangeError("constructor");
  }
  static x = new C();
}
