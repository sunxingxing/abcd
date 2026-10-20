export class C {
  f(a, b = globalThis.input.x, c = 0, d = -1) { return [this, a, b, c, d]; }
}
