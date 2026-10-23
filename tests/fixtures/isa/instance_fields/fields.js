export class A {
  x = 1;
  y = this.x + 1;
  z;
  constructor(v) { this.v = v + this.y; }
  get w() { return this.z; }
  m() { return this.x + this.y; }
}
export class Plain { s = "k"; t = null; u = 2.5; }
export class Shadow {
  x = 7;
  set x(v) { throw new Error("setter " + v); }
  get x() { return -1; }
}
export class Counter {
  n = 0;
  inc() { return ++this.n; }
}
