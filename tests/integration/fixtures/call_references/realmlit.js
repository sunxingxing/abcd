const o1 = {g: globalThis};
const o2 = {g: globalThis.k};
const a1 = [globalThis, 1];
function run() {
  let r;
  try {
    r = {g: globalThis, h: globalThis.k};
  } catch (e) {
    r = e;
  }
  return r;
}
const o3 = run();
export const result = [o1.g === globalThis, o2.g === globalThis.k, a1[0] === globalThis, a1[1],
  o3.g === globalThis, o3.h === globalThis.k].join(',');
