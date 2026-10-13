export class Derived extends __heritage() {
  static read() { return Derived; }
}
export function probe() { return Derived.read() === Derived; }
