// Await protocols whose continuation does not reload the awaited result into
// the accumulator: the result register feeds the next operation directly.
export async function accumulate(a) { let x = 0; x += await a; return x; }
export async function first(f, a) { return f(await a, 2); }
export async function sum(a, b) { return (await a) + b; }
// The THROW resume-mode constant is read from a register that already holds
// the integer 1 used by the surrounding code.
export async function tagged(f, a) { return f(1, await a); }
export async function ignored(f, a) { f(await a); return 1; }
export async function tested(a) { if (await a) { return 1; } return 2; }
export async function incremented(f, a) { return f(await a) + 1; }
