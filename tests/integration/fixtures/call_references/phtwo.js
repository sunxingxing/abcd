function run() {
  let r;
  try {
    r = globalThis.obj.f('a' + (globalThis.H ? globalThis.x.g.h : 0) + 'b' + (globalThis.missing ? 1 : globalThis.G(2)));
  } catch (e) {
    r = e === globalThis.sentinel ? 'sentinel' : e.constructor.name;
  }
  return r;
}
export const result = run();
