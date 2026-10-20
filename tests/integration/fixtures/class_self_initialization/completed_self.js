export class S {
  static inst = new S();
  static getInstance() { return S.inst; }
}
export const s = S.getInstance();
