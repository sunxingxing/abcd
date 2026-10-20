export class C {
  static fail() {
    const invoke = () => { void C; globalThis.log.push("member-throw"); throw new RangeError("member"); };
    return invoke();
  }
  static x = C.fail();
}
