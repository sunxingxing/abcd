export async function* after(xs, wait) {
  yield await wait('first');
  const [x] = xs;
  return x;
}
