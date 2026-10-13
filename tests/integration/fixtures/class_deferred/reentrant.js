export class Self {
  static read() { return Self; }
  constructor() { __reenter(Self.read); }
}
export function probe() { return new Self() instanceof Self; }
