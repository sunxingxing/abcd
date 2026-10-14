export function check(o) {
  if (o.first || (o.flag ? o.left : o.right) === o.test) {
    o.yes();
  } else {
    o.no();
  }
  return o.later;
}
export function andCheck(o) {
  if (o.first && (o.flag ? o.left : o.right) === o.test) {
    o.yes();
  } else {
    o.no();
  }
  return o.later;
}
export function nested(o) {
  if (o.first || (o.flag ? (o.inner ? o.left : o.middle) : o.right) === o.test) {
    o.yes();
  } else {
    o.no();
  }
  return o.later;
}
export function staged(o) {
  if (o.first || (o.before(), (o.flag ? o.left : o.right)) === o.test) {
    o.yes();
  } else {
    o.no();
  }
  return o.later;
}
