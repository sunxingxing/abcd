export let before;
class S {
  static inst = (before = S, new S());
  static getInstance() { return this.inst; }
}
export const s = S.getInstance();
