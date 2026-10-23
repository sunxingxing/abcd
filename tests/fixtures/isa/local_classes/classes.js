// Local classes whose members capture the creator's bindings (parameter,
// hoisted var, mutable let) and a module binding.
let top = 1;
export function param(cb) {
  class H { done(x) { return cb(x); } }
  return new H();
}
export function hoisted(c) {
  class H { m() { return y; } }
  var y = c;
  y = y + 1;
  return new H();
}
export function mutable(c) {
  let z = c;
  class H { m() { z = z + 1; return z; } }
  return new H();
}
export function moduleRead() {
  class H { m() { return top; } }
  top = top + 1;
  return new H();
}
