export var a;
export function early() { return typeof a + ':' + later(); }
export function later() { return 2; }
globalThis.__log = 'early:' + early();
a = 1;
export class K { m() { return a + 1; } static s() { return later() + a; } }
export function read() { return a; }
try { globalThis.__probe(); } catch (e) { globalThis.__log = globalThis.__log + '|' + String(e); }
