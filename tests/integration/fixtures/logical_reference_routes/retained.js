export function siblings(object, child, fallback, input) {
  return object.invoke(input.prefix + (child() || fallback) + input.middle + (child() || fallback));
}
