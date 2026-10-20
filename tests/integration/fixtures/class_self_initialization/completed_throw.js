class S {
  static inst = new S();
  static getInstance() { globalThis.log.push("get"); throw new RangeError(); }
}
export const s = S.getInstance();
