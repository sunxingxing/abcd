import { Mode } from './mode.js';
export class C {
  constructor(a, b = Mode.Normal, c = 0, d = 0, e = -1, f = 1,
              g = globalThis.Alignment.Center) {
    this.a = a;
    this.b = b;
    this.c = c;
    this.d = d;
    this.e = e;
    this.f = f;
    this.g = g;
  }
  static instance = new C(1);
}
