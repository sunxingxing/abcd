export class C { m() { return C; } static make() { return new C(); } }
export class S { static k = 3; m() { return S.k; } }
export function mk() { return new C().m() === C; }
export function statics() { return new S().m(); }
export function derived() { return C.make() instanceof C; }
export function early() { return S.k; }
try { globalThis.__probe(); } catch (e) { globalThis.__log = String(e); }
