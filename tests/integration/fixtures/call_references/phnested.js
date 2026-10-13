function run() {
  let r;
  try {
    r = globalThis.obj.f('a' + (globalThis.H ? (globalThis.missing ? 1 : globalThis.G(3)) : 0) + 'b');
  } catch (e) {
    r = e === globalThis.sentinel ? 'sentinel' : e.constructor.name;
  }
  return r;
}
export const result = run();
