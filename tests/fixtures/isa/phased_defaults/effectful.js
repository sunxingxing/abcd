export async function effectful(a, b, c = a()) { await b; return c; }
