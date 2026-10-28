export function choose(xs, select, read, valid) {
  let fallback = 1;
  for (const x of xs) {
    if (select(x)) {
      const n = read(x);
      return valid(n) && n >= 1 ? n : fallback;
    }
  }
  return fallback;
}
