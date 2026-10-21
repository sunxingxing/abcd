export class C {
  get value() { return globalThis.event('get'); }
  set value(v) { globalThis.event('set:' + v); }
  static x = undefined;
  read() { return this.value; }
}
