export class C {
  static get(unused) {
    function forward() { return () => C; }
    const unrelated = 4;
    return forward()(unrelated);
  }
  static x = C.get();
}
