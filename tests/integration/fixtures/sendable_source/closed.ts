export class Closed {
  value: number;
  constructor(value: number) {
    "use sendable";
    this.value = 0;
    this.value = value;
  }
  read() { return this.value; }
}
