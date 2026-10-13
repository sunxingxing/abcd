function run() {
  let r;
  try {
    r = globalThis.obj.f(globalThis.P + (globalThis.H ? globalThis.G(4) : 0) + globalThis.P);
  } catch (e) {
    r = e === globalThis.sentinel ? 'sentinel' : e.constructor.name;
  }
  return r;
}
export const result = run();
