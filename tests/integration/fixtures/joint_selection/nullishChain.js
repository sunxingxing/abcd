export function nullishChain(object, x, y, z) { return object.invoke(x ?? y ?? z); }
