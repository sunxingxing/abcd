function run() {
  let r;
  try {
    r = 'a' + (globalThis.H ? globalThis.G(7) : 0) + 'b' + (globalThis.missing ? 1 : globalThis.x.g.h);
  } catch (e) {
    r = e === globalThis.sentinel ? 'sentinel' : e.constructor.name;
  }
  return r;
}
export const result = run();
