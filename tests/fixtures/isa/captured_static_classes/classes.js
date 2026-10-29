// Classes whose methods capture the class binding (so the module creates
// the class inside an environment-cleanup try) and that also carry static
// initializers, accessors and instance fields.
export class Plain { static s = 1; static t = 2; get m() { return Plain.s * 10; } run() { return () => Plain.t; } }
export class Fields { x = 1; static s = 'f'; run() { return () => Fields.s + this.x; } }
export class Mixed { y = 2; static count = 0; static bump() { return ++Mixed.count; } get z() { return this.y + 1; } later() { return () => Mixed.bump(); } }
