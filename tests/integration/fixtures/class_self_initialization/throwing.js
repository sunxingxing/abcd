export class C {
  static x = (new C(), (() => { throw new RangeError("initializer"); })());
}
