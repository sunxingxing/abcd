export function moduleFunction(x) { return x + 9; }
export function make() {
  try { class Local {} return Local; } catch (error) { return error; }
}
export async function before(cb) {
  class H { on(x) { cb(x); } }
  await cb(1);
  return new H();
}
export async function after(cb) {
  await cb(1);
  class H { on(x) { return cb(x); } }
  return new H();
}
export function captured(value) {
  try { class H { read() { return value; } } return new H(); }
  catch (error) { return error; }
}
export function mutable(value) {
  try {
    class H { next() { value = value + 1; return value; } }
    const h = new H(); value = value + 2; return h;
  } catch (error) { return error; }
}
export function caught(value) {
  try { throw value; }
  catch (error) { class H { read() { return error; } } return new H(); }
}
export function escaped(value, cb) {
  try {
    class H { read() { return value; } }
    cb(H); return H.prototype.read;
  } catch (error) { return error; }
}
export function* generated(value) {
  try {
    class H { read() { return value; } }
    yield new H(); return H;
  } catch (error) { return error; }
}
export async function moduleReader(cb) {
  await cb(1);
  class H { read() { return moduleFunction(4); } }
  return new H();
}
