export class B {}
export class StringBranch extends B {
  constructor(d) {
    if (typeof d === 'string') { super(d); } else { return null; }
  }
}
export class BothBranches extends B {
  constructor(d) { if (d) { super(d); } else { super(); } }
}
export class EarlyObject extends B {
  constructor(d) { if (!d) { return {}; } super(d); }
}
export class ReturnValue extends B {
  constructor(init, value) { if (init) { super(init); } return value; }
}
export class BareReturn extends B {
  constructor(init) { if (init) { super(init); } return; }
}
export class Fallthrough extends B {
  constructor(init) { if (init) { super(init); } }
}
export class DoubleSuper extends B {
  constructor(d) { super(d); super(d); }
}
export class LoopSuper extends B {
  constructor(n) { while (n > 0) { super(n); n = n - 1; } }
}
export class ReadThis extends B {
  constructor(init) { if (init) { super(init); } this.x = 1; }
}
