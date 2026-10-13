export function logical(x, f) { return !!x && f(x.p); }
export function retained(x, sink) {
  if (!!x) {
    let text = !x.p ? 'default' : x.p;
    sink.log(text);
    sink.value = text;
    return text;
  }
  return 'empty';
}
export function booleanJoin(a, b, f) {
  let value = !a && b;
  f(value);
  return value;
}
export function multi(f, x) { return f(!!x.a && !!x.b && !!x.c); }
export function chain(x, f) {
  let p = x.a === x.b && x.a === x.c;
  let q = x.d === x.e && x.d === x.f;
  let value = !q && p;
  f(value);
  return value;
}
