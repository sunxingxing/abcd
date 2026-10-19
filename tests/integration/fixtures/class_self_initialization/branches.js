export class C {
  static x = globalThis.select ? new C() : new C();
}
