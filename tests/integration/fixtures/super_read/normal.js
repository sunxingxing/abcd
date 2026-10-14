export class Child extends globalThis.TestBase {
  run(arg) { return super.value(arg()); }
  read() { return super.value; }
  static run(arg) { return super.value(arg()); }
}
