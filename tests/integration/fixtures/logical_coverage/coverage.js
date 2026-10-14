export function retained(o) {
  const selected = o.test ? o.yes : o.no;
  return {value: selected};
}
export function short(o) {
  const selected = o.test && o.other ? o.yes : o.no;
  return {value: selected, later: o.later};
}
export function nested(o) {
  const selected = o.test ? (o.other ? o.yes : o.no) : o.fallback;
  return {before: o.before, value: selected, later: o.later};
}
export function argument(o) {
  const selected = o.test ? o.yes : o.no;
  return o.call({before: o.before, value: selected, later: o.later});
}
