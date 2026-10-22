class B {}
export class Twice extends B { constructor() { super(); super(); } }
export class Missing extends B { constructor() {} }
