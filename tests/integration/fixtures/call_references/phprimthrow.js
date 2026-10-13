function run() {
  let r;
  try {
    r = globalThis.obj.f('a' + (globalThis.H ? globalThis.G(5) : 0) + globalThis.Q);
  } catch (e) {
    r = e === globalThis.sentinel ? 'sentinel' : e.constructor.name;
  }
  return r;
}
export const result = run();
