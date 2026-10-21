export class C {
  static x = C.read();
  static read() { return globalThis.event('reentry'); }
  get value() { return C.x; }
}
