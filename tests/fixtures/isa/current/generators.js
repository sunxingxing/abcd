export function* g(x){yield x;return yield* x;}
export async function* ag(x){yield await x;return yield* x;}
export async function ai(x){for await(const a of x){if(a)return a;}return 0;}
