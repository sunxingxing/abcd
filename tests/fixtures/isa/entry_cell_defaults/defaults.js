// Literal and cell-backed defaults after the invocation entries (callee,
// new.target, this) are stored into a fresh environment for arrows.
export class View {
  constructor(parent, params, local = null, elmtId = -1) {
    this.parent = parent;
    this.local = local;
    this.id = elmtId;
    this.show = () => this.id + ':' + (this.local === null) + ':' + typeof this.parent;
  }
}
export function Made(a, b = 2) {
  const target = () => new.target === Made;
  return 'made:' + a + ':' + b + ':' + target();
}
export function sum(a, b = 3) {
  const read = () => a + b + ':' + (this === undefined);
  return read();
}
export function self(a, b = 'x') {
  const own = () => typeof self + ':' + (this === undefined) + ':' + a + b;
  return own();
}
export function many(a, b = 'q', c = 4, d = true) {
  const k = () => typeof this;
  return a + ':' + b + ':' + c + ':' + d + ':' + k();
}
