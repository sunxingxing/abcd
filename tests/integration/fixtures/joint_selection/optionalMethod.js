export function optionalMethod(object, a, d) { return object.invoke(a?.m() ?? d); }
