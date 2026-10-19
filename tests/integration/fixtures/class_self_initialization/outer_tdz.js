export class C {
  static x = C.read(outer);
  static read(value) { return value; }
}
let outer = 7;
