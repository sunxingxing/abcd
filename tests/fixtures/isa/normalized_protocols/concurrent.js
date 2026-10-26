export async function before(xs) {
  "use concurrent";
  const [x] = xs;
  await 0;
  return x;
}
export async function between(xs) {
  "use concurrent";
  await 0;
  const [x] = xs;
  await 0;
  return x;
}
