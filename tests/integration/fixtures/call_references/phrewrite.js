function run() {
  let r;
  try {
    r = globalThis.obj.f(globalThis.H ? globalThis.R(4) : globalThis.G(4));
  } catch (e) {
    r = e === globalThis.sentinel ? 'sentinel' : e.constructor.name;
  }
  return r;
}
export const result = run();
