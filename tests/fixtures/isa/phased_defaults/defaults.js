export async function plain(a, b, c = 8) { await a; return [b, c]; }
export async function multiple(a, b, c = 8, d = false, e = null, f = undefined) {
  await a;
  return [b, c, d, e, f];
}
export async function captured(a, b, c = 8) {
  const read = () => c;
  const before = read();
  await a;
  return [before, read(), b];
}
export async function caught(a, b, c = 8) {
  try { await a; return [b, c]; } catch (error) { return [error, c]; }
}
export const arrow = async (a, b, c = 8) => { await a; return [b, c]; };
export class Methods {
  async run(a, b, c = 8) { await a; return [b, c]; }
  static async run(a, b, c = 8) { await a; return [b, c]; }
}
export async function unused(a, b, c = 8) { await a; return b; }
export async function literals(a, b, c = 'ready', d = 2.5, e = true) { await a; return [b, c, d, e]; }
export async function mutable(a, b, c = 8) {
  const read = () => c;
  const before = read();
  c = b;
  await a;
  return [before, read()];
}
