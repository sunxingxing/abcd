class B { constructor(value) { this.value = value; } }
export class C extends B { constructor(value) { return super(value); } }
