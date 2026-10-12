function run() {
  let r;
  try {
    r = globalThis.obj.f(globalThis.missing ? globalThis.x.g.h : globalThis.G(2));
  } catch (e) {
    r = e === globalThis.sentinel ? 'sentinel' : e.constructor.name;
  }
  return r;
}
export const result = run();
