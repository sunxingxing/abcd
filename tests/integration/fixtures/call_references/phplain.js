function run() {
  let r;
  try {
    r = globalThis.obj.f(globalThis.x.g.h);
  } catch (e) {
    r = e === globalThis.sentinel ? 'sentinel' : e.constructor.name;
  }
  return r;
}
export const result = run();
