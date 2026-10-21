class B { constructor() { globalThis.event('super'); } }
export class C extends B {
  x = globalThis.event('derived');
  constructor() { super(); globalThis.event('body'); }
}
