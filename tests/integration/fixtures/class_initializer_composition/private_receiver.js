export class C {
  static context = new Map(globalThis.seed());
  static more = globalThis.event('more');
  get value() { return 17; }
  static answer() { return 42; }
}
