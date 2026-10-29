export class A {
  get v() { return this._v ?? 1; }
  set v(x) { this._v = x * 2; }
  static get s() { return 2; }
  static set s(x) { globalThis.__log = 'set:' + x; }
}
export function mk(x) {
  class B { get w() { return x + 3; } set w(y) { this.y = y; } }
  try { const b = new B(); b.w = 4; return [b.w, b.y]; } catch (e) { return 0; }
}
try { globalThis.__probe(); } catch (e) { globalThis.__log = 'caught'; }
