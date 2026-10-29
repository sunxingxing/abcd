class B { constructor(v) { if (v === 'boom') throw new Error('base'); this.v = v; } }
const K = 3;
export class D extends B { constructor(x) { super(x + K); this.x = x; } m() { return this.v; } }
export function make(x) { return new B(x); }
export function nested(x) { class C extends B { m() { return K + this.v; } } return new C(x); }
try { globalThis.__probe(); } catch (e) { globalThis.__log = e; }
