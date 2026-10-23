export class Base { constructor(...xs) { this.xs = xs; } }
export class Implicit extends Base {}
export class Spread extends Base { constructor(...xs) { super(...xs); this.n = xs.length; } }
export class List extends Array {}
export class Plain {}
export class FromPlain extends Plain {}
