class Base { constructor(v) { this.v = v; } }
export function mk() { class C { m() { return C; } } return new C().m() === C; }
export function statics(x) { class S { static k = 3; static j = 2; m() { return S.k + S.j + x; } } return new S().m(); }
export function derived(x) { class D extends Base { me() { return D; } } const d = new D(x); return [d.me() === D, d.v]; }
export function early() { class T { static peek() { return T; } } return T.peek() === T; }
