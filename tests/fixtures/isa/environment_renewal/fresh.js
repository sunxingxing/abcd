export async function values(xs, keep) {
  for (let value of xs) keep(() => value);
  await 0;
}
export async function keys(obj, keep) {
  for (let key in obj) keep(() => key);
  await 0;
}
export async function blocks(n, keep) {
  let i = 0;
  while (i < n) { let value = i++; keep(() => value); }
  await 0;
}
