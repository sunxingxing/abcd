export class A { static x = 1; static y = globalThis.__seed('A'); m() { return this.constructor.x + 1; } }
export class B { static list = [globalThis.__seed('B'), 2]; static z; get() { return 3; } }
try { globalThis.__probe('entry'); } catch (e) { globalThis.__log.push(['entry', e]); }
