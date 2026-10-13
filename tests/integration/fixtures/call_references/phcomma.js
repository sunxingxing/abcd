function run() {
  let r;
  try {
    r = globalThis.obj.f('a' + (globalThis.G(0), globalThis.H ? globalThis.x.g.h : 1) + 'b');
  } catch (e) {
    r = e === globalThis.sentinel ? 'sentinel' : e.constructor.name;
  }
  return r;
}
export const result = run();
