function run() {
  let r;
  try {
    r = globalThis.obj.f('a' + (globalThis.H ? globalThis.x.g.h : globalThis.G(6)) + 'b');
  } catch (e) {
    r = e === globalThis.sentinel ? 'sentinel' : e.constructor.name;
  }
  return r;
}
export const result = run();
