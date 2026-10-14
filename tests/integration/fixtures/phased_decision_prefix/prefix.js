export function check(o) {
  try {
    if (o.first || (o.flag ? o.left : o.right) === o.test) o.yes();
    else o.no();
    return o.later;
  } catch (e) {
    o.caught(e.name);
    return -1;
  }
}
export function boolean(o) {
  try {
    if (o.first || (!!o.flag && o.next)) o.yes();
    else o.no();
    return o.later;
  } catch (e) {
    o.caught(e.name);
    return -1;
  }
}
