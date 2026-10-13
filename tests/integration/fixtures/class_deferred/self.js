export class Self {
  static read() { return Self; }
  static forwarded() { return () => Self; }
}
export function probe() { return Self.read() === Self && Self.forwarded()() === Self; }
