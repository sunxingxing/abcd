export async function* one() { const x = await Promise.resolve(1); yield x; }
export async function* two(a) { let i = 0; i = a + 1; yield i; yield i * 2; }
