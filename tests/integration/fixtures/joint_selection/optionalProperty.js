export function optionalProperty(object, a, d) { return object.invoke(a?.b ?? d); }
