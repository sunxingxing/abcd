function run() {
  let r;
  try {
    r = globalThis.obj.f('a' + (globalThis.H ? globalThis.R(6) : 0) + 'b');
  } catch (e) {
    r = e === globalThis.sentinel ? 'sentinel' : e.constructor.name;
  }
  return r;
}
export const result = run();
