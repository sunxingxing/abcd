export async function collect(values) {
  let last;
  for (const value of values) last = await value.visit();
  return last;
}
