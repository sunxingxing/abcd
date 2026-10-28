class B { constructor(v) { if (v === 'boom') throw new Error('base'); this.v = v; } }
export class D extends B { constructor(x) { super(x + 1); this.x = x; } m() { return this.v; } }
export function make(x) { class E extends B { m() { return [this.v, super.constructor.name]; } } return new E(x); }
export function nested(x) { class G extends D { constructor(y) { super(y); this.g = y; } } return new G(x); }
