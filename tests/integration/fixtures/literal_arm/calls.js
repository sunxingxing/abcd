export function image(Image, input) {
  const v = input.value;
  return Image.create(v ? v : {id: 1, params: []});
}
export function fallback(obj, input) {
  const x = input.value;
  return obj.f(x || {p: []});
}
export function inline(obj, input) {
  return obj.f(input.value || {p: []});
}
export function conditional(obj, flag, g, h) {
  return obj.f(flag ? {p: g()} : {p: h()});
}
