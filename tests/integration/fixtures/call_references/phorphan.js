function run() {
  let r, s;
  try {
    r = globalThis.obj.f(typeof (globalThis.k() + (globalThis.H ? globalThis.x.g.h : 1)));
    s = globalThis.obj.f('a' + (globalThis.missing ? 1 : globalThis.G(9)) + 'b');
  } catch (e) {
    r = e === globalThis.sentinel ? 'sentinel' : e.constructor.name;
  }
  return [r, s].join('|');
}
export const result = run();
