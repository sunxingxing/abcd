export async function before(xs, wait) {
  const [x] = xs;
  await wait(x);
  return x;
}
export async function between(xs, wait) {
  await wait('first');
  const [x, y] = xs;
  await wait(x);
  return [x, y];
}
export async function after(xs, wait) {
  await wait('first');
  const [x] = xs;
  return x;
}
export async function multiple(xs, ys, wait) {
  const [x] = xs;
  await wait(x);
  const [y] = ys;
  await wait(y);
  return [x, y];
}
export async function caught(xs, wait) {
  try {
    const [x] = xs;
    await wait(x);
    const [y] = xs;
    await wait(y);
    return [x, y];
  } catch (e) { return e; }
}
