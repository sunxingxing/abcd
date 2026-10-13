function run() {
  let r;
  try {
    r = globalThis.obj.f(globalThis.H ? globalThis.G(1) : 0, globalThis.missing ? 1 : globalThis.x.g.h);
  } catch (e) {
    r = e === globalThis.sentinel ? 'sentinel' : e.constructor.name;
  }
  return r;
}
export const result = run();
