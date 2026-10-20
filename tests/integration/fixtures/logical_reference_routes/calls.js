export function optional(object, monitor, input) {
  return object.invoke(input.prefix + monitor.read()?.before + input.middle + monitor.read()?.after);
}
export function conditional(object, input) {
  return object.invoke(input.prefix + (input.first ? input.yes : input.no) + input.middle + (input.second ? input.yes : input.no));
}
export function earlier(object, left, right, monitor) {
  return object.invoke(left.value, right.value, "" + monitor.read()?.before + monitor.read()?.after);
}
