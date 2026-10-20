export function optionalArgument(object, a, x, d) {
  return object.invoke(a?.m(x) ?? d);
}
