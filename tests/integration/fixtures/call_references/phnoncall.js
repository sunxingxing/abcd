function run() {
  let r;
  try {
    r = globalThis.obj.nf(globalThis.H ? globalThis.x.g.h : globalThis.G(5));
  } catch (e) {
    r = e === globalThis.sentinel ? 'sentinel' : e.constructor.name;
  }
  return r;
}
export const result = run();
