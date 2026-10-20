export function nestedNullish(object, a, d) {
  return object.invoke(object.inner(a?.m() ?? d) - 1);
}
