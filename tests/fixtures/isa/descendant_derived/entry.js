class B { constructor(v) { this.v = v; } }
export class D extends B { constructor(x) { super(x + 1); this.x = x; } m() { return this.v; } }
export function make(x) { return new D(x); }
export function nested(x) { return new D(x * 2); }
try { globalThis.__probe(); } catch (e) { globalThis.__log = e; }
