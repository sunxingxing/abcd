export class C {
  get value() { return C; }
  set value(v) { this.stored = v; }
  static get current() { return C; }
  static x = new C();
}
