export class First { static read() { return value; } }
export class Second { static read() { return value; } }
__observe(First.read);
__observe(Second.read);
let value = __initialize(First);
export function probe() { return First.read() + Second.read(); }
