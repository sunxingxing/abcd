export let value = 7;
export class Base {
  constructor(...values) {
    this.values = values;
    this.target = new.target;
  }
}

export function setValue(next) {
  value = next;
}
