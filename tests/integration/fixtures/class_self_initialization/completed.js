class S {
  static inst = new S();
  static getInstance() { return this.inst; }
}
export const s = S.getInstance();
