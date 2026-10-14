export class Outer {
  factory(value) {
    return class Local {
      read() { return value; }
    };
  }
}
