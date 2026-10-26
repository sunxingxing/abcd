export async function* before(xs, wait) {
  const [x] = xs;
  await wait(x);
  yield x;
  return x;
}
export async function* between(xs, wait) {
  yield 'first';
  const [x] = xs;
  await wait(x);
  yield x;
  return x;
}
